// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui_heater_icon_binder.h"
#include "ui_observer_guard.h"

#include "async_lifetime_guard.h"
#include "callout_layout.h"
#include "panel_widget.h"

#include <array>
#include <string>
#include <vector>

namespace helix {

class PrinterImageWidget : public PanelWidget {
  public:
    PrinterImageWidget();
    ~PrinterImageWidget() override;

    void attach(lv_obj_t* widget_obj, lv_obj_t* parent_screen) override;
    void detach() override;
    /// Records the granted cell span (a single cell draws no callouts) and
    /// schedules a relayout of the chips.
    void on_size_changed(int colspan, int rowspan, int width_px, int height_px) override;
    /// Factory-registration key. Exposed so callers scanning a heterogeneous
    /// widget list can match on id() and static_cast, instead of dynamic_cast —
    /// the firmware builds -fno-rtti.
    static constexpr const char* WIDGET_ID = "printer_image";

    const char* id() const override {
        return WIDGET_ID;
    }

    /// Called when panel activates — re-check if printer image changed in settings
    void on_activate();

    /// Reload printer image and printer info subjects from config
    void reload_from_config();

    /// Re-check printer image setting and update the displayed image
    void refresh_printer_image();

    /// XML event callback — opens printer manager overlay
    static void printer_manager_clicked_cb(lv_event_t* e);

    /// XML event callbacks for the live callout chips — one per heater/fan/light.
    /// The toolhead chip (merged nozzle+fan) reuses printer_callout_nozzle_cb.
    static void printer_callout_nozzle_cb(lv_event_t* e);
    static void printer_callout_bed_cb(lv_event_t* e);
    static void printer_callout_chamber_cb(lv_event_t* e);
    static void printer_callout_fan_cb(lv_event_t* e);
    static void printer_callout_light_cb(lv_event_t* e);

  private:
    /// Points `img` at the pre-scaled copy for its current size, if one is on disk.
    /// Returns false when the widget has no resolved size yet, or nothing is cached
    /// at that size, leaving the caller to fall back to the tier image.
    bool try_set_exact_size_source(lv_obj_t* img);

  protected:
    void on_hooked_root_deleted() override;

  private:
    lv_obj_t* widget_obj_ = nullptr;
    lv_obj_t* parent_screen_ = nullptr;

    // Persistent disk cache for exact-size printer image
    lv_timer_t* cache_timer_ = nullptr;
    // Defers the image refresh out of the synchronous attach()/on_activate()
    // path: lv_image_set_inner_align forces lv_obj_update_layout, which cascades
    // into the parent grid's grid_update. Running that during a panel rebuild
    // (mid-rebuild grid) walked the freed descriptor off the heap end (#983/#1025).
    lv_timer_t* refresh_timer_ = nullptr;
    std::string current_source_path_; // Resolved source image (LVGL path)
    /// What lv_image_set_src was last given, so a repeat resolve to the same file
    /// does not invalidate the widget for an identical image.
    std::string current_displayed_path_;
    /// Natural size of the source, read once per path: every relayout needs it
    /// (an untagged image's aspect, a user tag's size guard), and a decoder
    /// info call opens the file.
    std::string natural_size_path_;
    int natural_w_ = 0;
    int natural_h_ = 0;

    /// Guards the cache-generation continuation, which runs from a worker thread
    /// and touches this widget's LVGL tree.
    helix::AsyncLifetimeGuard lifetime_;

    /// One cache generation at a time. Every navigation back to the panel schedules
    /// another cache check, and without this a second job would redo work already
    /// running for the same source and size.
    bool cache_job_inflight_ = false;

    /// Re-resolves the image when the printer type settles mid-session:
    /// auto-detection finishes after the home panel is built on a fresh
    /// install, so attach()'s one-shot resolve still shows the generic
    /// silhouette unless a type change re-triggers it.
    ObserverGuard printer_type_observer_;

    void schedule_image_refresh();
    void schedule_cache_check();
    void check_or_generate_cache();

    void handle_printer_manager_clicked();

    /// Live callouts: PrinterState -> chip subjects -> measured layout.
    void arm_callout_observers();
    void update_callouts();         ///< subjects -> chip text/shown; relayout on change
    void schedule_callout_layout(); ///< one-shot deferred apply_callout_layout()
    void cancel_callout_timer();
    void apply_callout_layout(); ///< measure, decide, position
    /// Moves and sizes printer_image to `moved`, or back to filling its
    /// container when null; refreshes the image only when the rect changes.
    void place_printer_image(const CalloutRect* moved);
    void set_glow_pulse(bool on); ///< start/stop the bed glow's opacity pulse
    void handle_callout_clicked(CalloutKind kind);
    /// Recovers the widget from a chip's click (chip -> callout_layer -> printer_container).
    static void route_callout_click(lv_event_t* e, CalloutKind kind);

    lv_timer_t* callout_timer_ = nullptr;
    // Own copies of the granted span: on_size_changed() may be reached directly,
    // which leaves the base class's recorded size at zero.
    int callout_colspan_ = 0;
    int callout_rowspan_ = 0;
    int callout_spin_pct_ = -1; ///< speed the fan icons spin at; -1 = not yet applied
    bool callout_glow_pulsing_ = false;
    /// lv_line keeps the pointer it is given, so each leader line's points
    /// (point, elbow, chip edge) live here, one set per CalloutKind from Nozzle to Light.
    std::array<std::array<lv_point_precise_t, 3>, 5> callout_line_pts_{};
    std::vector<ObserverGuard> callout_observers_;
    /// Each text chip's full text, indexed by CalloutKind. Its subject carries the
    /// compact form while the chips are pinned; measuring reads this.
    std::array<std::string, 6> callout_full_text_{};
    SubjectLifetime bed_temp_lt_, bed_target_lt_, chamber_temp_lt_, chamber_target_lt_;
    helix::ui::HeaterIconBinder nozzle_binder_, bed_binder_, chamber_binder_;
};

} // namespace helix
