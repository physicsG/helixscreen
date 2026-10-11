// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui_observer_guard.h"
#include "ui_widget_ref.h"

#include "axis.h"
#include "hold_repeat_timer.h"
#include "jog_coalescer.h"
#include "motion_presets.h"
#include "overlay_base.h"
#include "static_panel_registry.h"
#include "subject_managed_panel.h"
#include "ui/ui_modal_guard.h"

#include <optional>

/**
 * @file ui_panel_motion.h
 * @brief Motion panel - XYZ movement and homing control
 *
 * Overlay panel for jogging the printer head in X/Y/Z directions and homing axes.
 * Uses OverlayBase pattern with lifecycle hooks.
 */

// Jog mode: determines inner/outer ring distances for XY pad and Z buttons
namespace helix {

struct AxisBounds;

/// Keypad parameters for tapping one axis readout in the motion header.
struct AxisKeypadParams {
    float min_value;     ///< Axis envelope minimum (mm)
    float max_value;     ///< Axis envelope maximum (mm)
    float seed;          ///< COMMANDED position (mm); always commanded, even in actual mode
    bool allow_negative; ///< True only when the axis minimum is below zero
};

/// Seed and bounds for one axis' coordinate keypad. Empty when the axis
/// envelope is unknown: an absolute move would have nothing to clamp against.
std::optional<AxisKeypadParams> keypad_params_for_axis(const AxisBounds& bounds, Axis axis,
                                                       double commanded_mm);

/// Whether a Z button driving G-code direction `direction_mm` (after the
/// bed_moves inversion) can still move: true only when Z is homed with known
/// bounds and the commanded `z` already
/// sits at the bound that direction drives toward, within AxisMove::EPSILON_MM.
/// A partial move is possible anywhere else, so the button stays enabled.
bool z_direction_blocked(bool z_homed, bool bounds_known, double z, double z_min, double z_max,
                         double direction_mm);

/// One pointer event on the Bed tab's touch surface.
enum class BedTouch { Pressed, Pressing, Released, Lost };

enum class JogMode { Fine = 0, Coarse = 1, Turbo = 2 };
constexpr int JOG_MODE_COUNT = 3;

// Inner/outer distance pair for each mode. Labels are owned buffers so the
// struct can travel by value out of the settings lookup.
struct JogModeDistances {
    float inner;
    float outer;
    char inner_label[16];
    char outer_label[16];
};

/// Inner and outer ring distances for a jog mode, in millimetres.
/// Values come from settings; the labels are formatted for display.
JogModeDistances get_jog_mode_distances(JogMode mode);

inline const char* jog_mode_name(JogMode mode) {
    static const char* names[] = {"Fine", "Coarse", "Turbo"};
    int idx = static_cast<int>(mode);
    if (idx < 0 || idx >= JOG_MODE_COUNT)
        return "Unknown";
    return names[idx];
}

// Jog direction
enum class JogDirection {
    N,  // +Y
    S,  // -Y
    E,  // +X
    W,  // -X
    NE, // +X+Y
    NW, // -X+Y
    SE, // +X-Y
    SW  // -X-Y
};
} // namespace helix

class MotionPanel : public OverlayBase {
  public:
    MotionPanel();
    ~MotionPanel() override;

    // === OverlayBase interface ===
    void init_subjects() override;
    void deinit_subjects();
    void register_callbacks() override;
    const char* xml_component() const override {
        return "motion_panel";
    }
    /// Freed on close; the next open rebuilds it.
    bool destroy_on_close() const override {
        return true;
    }
    lv_obj_t* create(lv_obj_t* parent) override;
    const char* get_name() const override {
        return "Motion Panel";
    }

    // === Lifecycle hooks ===
    void on_activate() override;
    void on_deactivating(DeactivateReason reason) override;
    void on_ui_destroyed() override;

    // === Public API ===
    lv_obj_t* get_panel() const {
        return overlay_root_;
    }
    void set_position(float x, float y, float z);
    helix::JogMode get_jog_mode() const {
        return current_mode_;
    }

    /// Jog one zone. Returns false when nothing was dispatched (fully clamped
    /// at a limit, or the send was refused) - a hold-to-repeat reading false
    /// stops repeating. is_repeat is false for a tap and for the first jog of
    /// a hold (a fresh press); a fresh press fully clamped at a limit warns.
    bool jog(helix::JogDirection direction, float distance_mm, bool is_repeat = false);
    void home(char axis);

    /// Jog one Z button. Same false-means-refused contract as jog(), and the
    /// same is_repeat meaning (false = the initial jog of this press).
    bool handle_z_button(const char* name, bool is_repeat = false);
    void set_jog_mode(helix::JogMode mode); // Switch between Fine/Coarse/Turbo jog mode

    /// The Z hold timer, for the XML pressed/released/press_lost callbacks.
    /// Also the test seam: poll() drives the repeat without a periodic timer,
    /// which the unit-test harness never runs.
    helix::HoldRepeatTimer& z_hold_timer() {
        return z_hold_timer_;
    }

    /// Arm the Z hold repeat for a named button press (the XML pressed event).
    /// `button` is the pressed widget, when known.
    void begin_z_hold(const char* button_name, lv_obj_t* button = nullptr);

    /// Flip the persisted commanded/actual coordinate preference. The panel's
    /// observer on the settings subject re-renders the readouts.
    void toggle_coordinate_source();

    /// Open the coordinate keypad for one axis ('x'/'y'/'z'), seeded with the
    /// COMMANDED position and bounded by the axis envelope. Refuses (no-op)
    /// while jogging is gated off (not connected or klippy not ready).
    void open_axis_keypad(char axis);

    /// Keypad confirm: home the axis if needed, then dispatch the absolute
    /// target for that one axis.
    void request_axis_target(char axis, double mm);

    /// Select the content tab: 0=Jog, 1=Move, 2=Bed. Jog is re-selected every
    /// time the panel opens.
    void set_motion_tab(int tab);

    /// Move tab: send the toolhead to one of the nine bed grid positions. The
    /// target is computed at tap time; unknown axis bounds warn instead of
    /// moving, and an unhomed machine homes first.
    void handle_preset(helix::MotionPreset preset);

    /// Move tab: run the ParkToolhead macro when the slot resolves to one,
    /// else lift Z and park over the rear of the plate (plate_rear_park()).
    void handle_park();

    /// Park with no macro: over the rear of the plate, after lifting Z by
    /// PARK_Z_LIFT_MM (capped at travel) when @p lift_z. Every axis must
    /// already be homed.
    void park_over_plate(bool lift_z);

    /// Move tab: raise the shared Disable Motors confirmation.
    void handle_motors_off();

    /// Bed tab: one pointer event at @p screen_point on the touch surface
    /// @p area. A tap moves on release to the press point; travel past a few
    /// px makes it a drag, and the head follows the finger (latest target
    /// wins), with the release point sent only if it differs from the last.
    void handle_bed_touch(helix::BedTouch phase, lv_obj_t* area, lv_point_t screen_point);

    /// Bed tab: send the toolhead to @p target (XY), raising Z to @p lift_z
    /// first when set. Unhomed XY homes first, and then never lifts.
    void commit_bed_target(helix::AxisTarget target, std::optional<double> lift_z);

    /// Bed tab: size the plate to the bed's aspect inside the touch surface
    /// and place the marker. No-op while another tab is showing.
    void layout_bed_map();

    /// Clamp one axis against its bounds. Partial travel is silent; a fully
    /// clamped FRESH press (anything but a hold repeat tick) warns every time
    /// with the limit it hit. Returns the permitted delta, 0.0 when blocked.
    double clamp_axis_delta(helix::Axis axis, double current, double uncommitted, double delta,
                            float min, float max, bool fresh_press);
    /// The G-code envelope the clamps and the Z-button state use: the nominal
    /// one inset by GCODE_EDGE_MARGIN_MM. The keypad offers the nominal limits.
    helix::AxisBounds clamp_bounds();

    /// Raise the "at its limit" warning for an axis refused at the clamp bound
    /// on its max (`at_max`) or min side.
    void warn_axis_limit(helix::Axis axis, float clamp_bound, bool at_max);

  private:
    // RAII subject manager - auto-deinits all registered subjects on destruction
    SubjectManager subjects_;

    lv_subject_t pos_x_subject_{};
    lv_subject_t pos_y_subject_{};
    lv_subject_t pos_z_subject_{};          // Commanded Z position
    lv_subject_t z_axis_label_subject_{};   // "Bed" or "Print Head"
    lv_subject_t z_up_icon_subject_{};      // "arrow_expand_up" or "arrow_up"
    lv_subject_t z_down_icon_subject_{};    // "arrow_expand_down" or "arrow_down"
    lv_subject_t z_large_label_subject_{};  // "10mm" or "1mm" (large Z button label)
    lv_subject_t z_small_label_subject_{};  // "1mm" or "0.1mm" (small Z button label)
    lv_subject_t jog_mode_fine_active_{};   // 1 when Fine mode active
    lv_subject_t jog_mode_coarse_active_{}; // 1 when Coarse mode active
    lv_subject_t jog_mode_turbo_active_{};  // 1 when Turbo mode active
    // Per-axis homing state (0=unhomed, 1=homed) for declarative bind_style:
    // the coordinate readouts mute an axis whose position is not trustworthy.
    lv_subject_t motion_x_homed_{};
    lv_subject_t motion_y_homed_{};
    lv_subject_t motion_z_homed_{};
    // Z button limit state (1=render disabled): a further move in that
    // button's G-code direction is impossible. up/down are screen directions,
    // the bed_moves inversion inside update_z_button_blocked() maps them.
    lv_subject_t motion_z_up_blocked_{};
    lv_subject_t motion_z_down_blocked_{};
    // Content tab selection (0=Jog, 1=Move, 2=Bed). zone_tab instances bind
    // one active subject each and their labels are subject-bound, matching the
    // AMS environment strip that shares the component.
    lv_subject_t motion_tab_subject_{};
    lv_subject_t motion_tab_active_[3]{};
    lv_subject_t motion_tab_label_[3]{};
    char motion_tab_label_buf_[3][32];
    // Bed tab: readout text under the plate, and 1 when the plate is round.
    lv_subject_t motion_bed_readout_{};
    char motion_bed_readout_buf_[128];
    lv_subject_t motion_bed_circular_{};
    /// Fill the tab label buffers (and subjects, once they exist) in the
    /// current language.
    void refresh_tab_labels();
    int motion_tab_ = 0;
    char pos_x_buf_[32];
    char pos_y_buf_[32];
    char pos_z_buf_[32];
    char z_axis_label_buf_[64];
    char z_up_icon_buf_[24];
    char z_down_icon_buf_[24];
    char z_large_label_buf_[8];
    char z_small_label_buf_[8];
    bool bed_moves_ = false; // If true, invert Z direction (arrows match bed movement)

    helix::JogMode current_mode_ = helix::JogMode::Coarse;
    float current_x_ = 0.0f;
    float current_y_ = 0.0f;
    float current_z_ = 0.0f; // Gcode (commanded) Z position

    int gcode_z_centimm_ = 0;

    lv_obj_t* jog_pad_ = nullptr;

    helix::JogCoalescer jog_coalescer_;

    /// The Move tab's Disable Motors confirmation. The guard hides the dialog
    /// if the panel is destroyed while it is open.
    helix::ui::ModalGuard motors_off_dialog_;
    /// Z the toolhead will sit at when the latest target's script starts,
    /// captured at enqueue time. Feeds move_to's travel-before-descend
    /// ordering; delta moves ignore it.
    double target_start_z_ = 0.0;

    /// Whether a repeat tick has fired for the current Z-button hold: the
    /// first tick is that press's initial jog, later ones are repeats.
    bool z_hold_repeated_ = false;

    /// Hold-to-repeat for the four Z buttons. The pad owns its own repeat
    /// (it is the only one that can see the press point move between zones);
    /// both use helix::HoldRepeatTimer so the timing logic is shared.
    helix::HoldRepeatTimer z_hold_timer_;
    char z_hold_button_[16] = {};
    /// The held Z button. A button disabled under the finger never receives
    /// RELEASED, so LVGL would leave it drawn pressed.
    helix::ui::WidgetRef z_hold_obj_;

    /// stop_hold_repeat() sweeps both this timer and the pad's on every
    /// teardown path.
    void stop_hold_repeat();
    static bool z_hold_fire(void* user_data);

    /// The Bed tab's plate (preset_area()) and shape, or nullopt while the
    /// bounds are unknown or degenerate.
    struct BedPlate {
        helix::AxisBounds area;
        bool circular;
    };
    std::optional<BedPlate> bed_plate() const;

    /// Target under @p screen_point on the touch surface @p area.
    std::optional<helix::AxisTarget> bed_target_at(lv_obj_t* area, lv_point_t screen_point) const;

    /// Z a Bed-tab move would lift to now, or nullopt for none (or Z unhomed).
    std::optional<double> bed_lift_z();

    /// Move the toolhead marker to the commanded XY.
    void update_bed_marker();

    /// Re-render the Bed tab readout: the target under a dragging finger and
    /// the clearance lift a commit would make.
    void refresh_bed_readout();

    // Bed tab press tracking: where the press landed, and whether it has
    // travelled far enough to be a drag. The drag target feeds the readout.
    bool bed_pressing_ = false;
    bool bed_dragging_ = false;
    lv_point_t bed_press_point_{};
    std::optional<helix::AxisTarget> bed_drag_target_;
    /// The last target this gesture sent; empty until its first move.
    std::optional<helix::AxisTarget> bed_sent_target_;
    /// The Z every move of this gesture lifts to, decided at its first move.
    std::optional<double> bed_gesture_lift_z_;
    /// A move of this gesture was refused; it sends nothing more.
    bool bed_gesture_failed_ = false;
    /// Stop tracking the finger: no press, no drag target, readout at rest.
    void end_bed_gesture();

    /// Send one of the current gesture's targets, lifting only on the first.
    void send_bed_gesture_target(const helix::AxisTarget& target);

    /// Gating backstop behind the Move tab's XML disabled bindings: commands
    /// are allowed only while nav buttons are enabled (connected + klippy
    /// ready) and no print is active.
    bool moves_allowed() const;

    /// Write motion_tab_ into the tab subject and the three per-tab active
    /// subjects.
    void sync_motion_tab_subjects();

    // Route a tap/flush through the coalescer and send if idle. Returns
    // whether the move was dispatched or accepted as pending.
    bool dispatch_jog(const helix::AxisMove& delta);
    // Route an absolute target through the coalescer and send if idle.
    bool dispatch_target(const helix::AxisTarget& target);
    // Send one coalesced move (delta or target); ack/error callbacks re-enter
    // the coalescer. Returns false when there is no API to send through.
    bool send_jog_move(const helix::JogCoalescer::CoalescedMove& move);

    ObserverGuard position_x_observer_;
    ObserverGuard position_y_observer_;
    ObserverGuard gcode_z_observer_;
    ObserverGuard gcode_z_offset_observer_;
    ObserverGuard live_position_observer_x_;
    ObserverGuard live_position_observer_y_;
    ObserverGuard live_position_observer_z_;
    ObserverGuard coordinate_mode_observer_;
    ObserverGuard language_observer_;
    ObserverGuard bed_moves_observer_;
    ObserverGuard homed_axes_observer_;
    ObserverGuard jog_ready_observer_;

    // Actual (live) positions in mm; the readouts show these when the
    // coordinate preference is "actual", the commanded current_x_/y_/z_
    // otherwise. Keypad seed and jog math always use the commanded values.
    float live_x_ = 0.0f;
    float live_y_ = 0.0f;
    float live_z_ = 0.0f;

    /// Axis whose keypad is open ('x'/'y'/'z'); the keypad callback is a bare
    /// function pointer, so the axis travels through the panel.
    char keypad_axis_ = 'x';

    /// Re-render all three readout subjects from the selected source.
    void refresh_position_display();

    /// Recompute the Z-button disabled subjects from the predicted Z, homed
    /// state, bounds and bed_moves inversion.
    void update_z_button_blocked();

    /// ui_keypad_show() confirm callback.
    static void on_axis_keypad_value(float value, void* user_data);

    void setup_jog_pad();

    /// Re-fit the pad square inside its wrapper. Portrait also clamps the
    /// wrapper to the square so the growing Z column absorbs the leftover
    /// width; landscape centres the pad in its growing wrapper untouched.
    void fit_jog_pad();
    void register_position_observers();

    // Enable/dim the jog pad from the current nav_buttons_enabled state
    // (connected AND klippy ready). Jogging while not ready is refused at the
    // API layer; this makes the pad visibly unavailable to match.
    void update_jog_pad_enabled();

    static bool jog_pad_jog_cb(helix::JogDirection direction, float distance_mm, bool is_repeat,
                               void* user_data);
    static void jog_pad_home_cb(void* user_data);
    // Position observers use lambda-based observer factory (no static callbacks needed)

    void update_z_axis_label(bool bed_moves);
    void update_z_button_labels(); // Update Z button text for current mode

    /// Which source the readouts render: 1 = actual (live) position,
    /// 0 = commanded. Mirrors the persisted preference subject.
    bool show_actual_ = false;
};

inline MotionPanel& get_global_motion_panel() {
    return helix::lazy_global<MotionPanel>("MotionPanel");
}
