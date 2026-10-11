// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui_observer_guard.h"
#include "ui_timer_guard.h"

#include "belt_gating.h"
#include "belt_tension_calibrator.h"
#include "belt_tension_types.h"
#include "memory_utils.h"
#include "operation_timeout_guard.h"
#include "overlay_base.h"
#include "platform_capabilities.h"
#include "resonance_console.h"
#include "static_panel_registry.h"
#include "subject_managed_panel.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class IMoonrakerAPI;

// Forward declaration - see ui_frequency_response_chart.h
struct ui_frequency_response_chart_t;

namespace helix {
class IMoonrakerClient;
}

/// Test hook: tests/unit/test_belt_tension_panel_states.cpp reads the chart
/// and ghost series, which no subject exposes.
class BeltPanelFixture;

/**
 * @file ui_panel_belt_tension.h
 * @brief Belt tension panel: two resonance sweeps, compared
 *
 * Runs Klipper's TEST_RESONANCES down each CoreXY belt diagonal and compares
 * the resulting curves. Four view states drive one XML layout:
 * - START: gate result + hardware summary + start button
 * - RUNNING: one sweep at a time, live chart cursor + elapsed time
 * - RESULTS: strongest peak pair, verdict, similarity, ghost of the previous run
 * - ERROR: the failure, with retry
 *
 * START's action is gated on `bt_can_start`, which is driven solely by
 * evaluate_belt_gate() plus a klippy-READY leg. Hiding the Advanced-panel menu
 * row is not a gate: the panel is still reachable by `ctl navigate`, by a deep
 * link, and by a printer whose accelerometer drops out after entry. Binding
 * the button's disabled state to a subject holds in all three cases.
 *
 * ## Usage:
 * ```cpp
 * auto& panel = get_global_belt_tension_panel();
 * panel.init_subjects();
 * panel.create(screen);
 * panel.show();
 * ```
 */
class BeltTensionPanel : public OverlayBase {
  public:
    enum class ViewState {
        START = 0,   ///< gate result + hardware summary + start button
        RUNNING = 1, ///< sweeping one path
        RESULTS = 2, ///< both paths measured, compared
        ERROR = 3,
    };

    /// A sweep that goes this long without a progress line is treated as
    /// dead: Klipper prints a "Testing frequency" line every second, so two
    /// minutes of silence means the run is not coming back.
    static constexpr uint32_t STALL_TIMEOUT_MS = 120000;

    /// Pairs and unpaired peaks named in the results facts and marked on the
    /// chart, strongest first.
    static constexpr size_t MAX_LISTED_PEAKS = 3;

    BeltTensionPanel() = default;
    ~BeltTensionPanel() override;

    //
    // === OverlayBase Interface ===
    //

    void init_subjects() override;
    void deinit_subjects();
    lv_obj_t* create(lv_obj_t* parent) override;

    const char* xml_component() const override {
        return "panel_belt_tension";
    }

    const char* get_name() const override {
        return "Belt Tension";
    }

    void on_activate() override;
    void on_deactivating(DeactivateReason reason) override;
    void cleanup() override;
    void on_ui_destroyed() override;

    //
    // === Public API ===
    //

    using OverlayBase::show;
    void show();
    void set_api(helix::IMoonrakerClient* client, IMoonrakerAPI* api);

    /// Pin the platform tier the chart decision uses, so tests can force
    /// EMBEDDED (no chart at all). Call before create(); production reads
    /// PlatformCapabilities::detect() when the first measurement starts.
    void set_render_tier_for_test(helix::PlatformTier tier, bool supports_animations);

    /// Pin the memory the low-memory check reads. Production reads the host's
    /// own memory, which is the printer's: the check only runs co-located.
    void set_memory_for_test(const helix::MemoryInfo& mem);

    //
    // === Event Handlers (public for XML callbacks) ===
    //

    void handle_start_clicked();
    void handle_stop_clicked();
    void handle_retry_clicked();
    void handle_retest_clicked(helix::calibration::BeltPath path);

  private:
    /// One path's measurement history. `previous` holds the curve the
    /// re-test is measuring against, so RESULTS can ghost it.
    struct PathRun {
        helix::calibration::BeltCurve curve;
        helix::calibration::BeltCurve previous;
        bool has = false;
        bool has_previous = false;
        uint32_t measured_at_ms = 0;
    };

    void set_view_state(ViewState state);
    void on_hardware_detected(const helix::calibration::BeltTensionHardware& hw);
    void on_error(const std::string& message);

    //
    // === Run orchestration ===
    //

    /// Queue paths for measurement and switch to RUNNING. `clear_previous`
    /// drops both paths' ghost history (Start runs fresh); a single-path
    /// re-test keeps the sibling's.
    void begin_run(const std::vector<helix::calibration::BeltPath>& queue, bool clear_previous);
    void start_next_measurement();
    void on_sweep_progress(int percent, float freq_hz);
    void on_sweep_complete(helix::calibration::BeltPath path, helix::calibration::BeltCurve curve);
    void on_sweep_error(const std::string& message);
    /// The stall guard fired: no progress line for STALL_TIMEOUT_MS.
    void on_stall();
    /// Queue empty: compare the runs and populate RESULTS, or error when a
    /// sweep returned too little in-band frequency data to compare.
    void finish_run();
    void populate_results(const helix::calibration::BeltComparison& cmp);
    /// Cancel the sweep, the stall guard, the elapsed timer and the queue.
    void cancel_run();
    void back_to_start();

    //
    // === Subject refresh ===
    //

    /// bt_note_* from the runs. Notes read "sweeping" for the running path, an
    /// age ("just now", "3 min ago") plus " · was N%" when a previous curve
    /// exists.
    void refresh_notes();
    void refresh_run_detail();
    void start_elapsed_timer();

    //
    // === Gate ===
    //

    /// The single place the gate is computed. Nothing else may decide whether
    /// Start is live.
    void refresh_gate();
    /// Attach the gate's subject observers once. Idempotent: the accelerometer
    /// subject is owned by PrinterCapabilitiesState and may not exist yet the
    /// first time this runs, so activation retries.
    void ensure_gate_observers();
    /// Fetch the klippy UDS path from Moonraker and probe co-location.
    void probe_klippy_socket();
    /// Ask the printer its [resonance_tester] range for bt_hw_sweep.
    void query_hw_facts();

    //
    // === Chart ===
    //

    /// Series color for a path: primary for A, warning for B. One helper so a
    /// dedicated path palette lands in one place.
    [[nodiscard]] static lv_color_t path_color(helix::calibration::BeltPath path);
    /// Create the chart on first use (never on EMBEDDED). Returns null when
    /// the tier forbids a chart or the view carries no host.
    ui_frequency_response_chart_t* ensure_chart();
    void destroy_chart();
    /// Re-read belt_path_a/b into the chart's series after a theme change.
    void apply_path_colors();
    /// Park the chart obj in the RUNNING host; called when a new run starts.
    void chart_to_running_host();
    /// Fit the chart's axes to the curves it holds (or the sweep range before any).
    void push_chart_data();
    void push_chart_markers(const helix::calibration::BeltComparison& cmp);
    void run_after_ram_check(std::function<void()> go);

    // Subject manager for RAII cleanup
    SubjectManager subjects_;

    // View state subject lives in the .cpp (static) but registers here.
    lv_subject_t can_start_subject_{};
    lv_subject_t gate_message_subject_{};
    char gate_message_buf_[128] = {};

    lv_subject_t hw_kinematics_subject_{};
    char hw_kinematics_buf_[64] = {};
    lv_subject_t hw_accel_subject_{};
    char hw_accel_buf_[64] = {};
    lv_subject_t hw_sweep_subject_{};
    char hw_sweep_buf_[64] = {};

    lv_subject_t run_title_subject_{};
    char run_title_buf_[64] = {};
    lv_subject_t run_detail_subject_{};
    char run_detail_buf_[96] = {};
    lv_subject_t running_path_subject_{};

    lv_subject_t note_a_subject_{};
    char note_a_buf_[64] = {};
    lv_subject_t note_b_subject_{};
    char note_b_buf_[64] = {};

    lv_subject_t verdict_subject_{};
    lv_subject_t verdict_text_subject_{};
    char verdict_text_buf_[128] = {};
    lv_subject_t facts_subject_{};
    char facts_buf_[96] = {};
    lv_subject_t unpaired_subject_{};
    char unpaired_buf_[128] = {};
    lv_subject_t has_unpaired_subject_{};
    lv_subject_t similarity_subject_{};
    char similarity_buf_[16] = {};
    lv_subject_t chart_available_subject_{};
    lv_subject_t error_message_subject_{};
    char error_message_buf_[256] = {};

    // Gate observers. Every one carries PrinterState's own SubjectLifetime -
    // the observe<V> factory requires it as its fourth parameter; a
    // default-constructed token leaves the guard tokenless (#705).
    ObserverGuard accel_observer_;
    ObserverGuard print_active_observer_;
    ObserverGuard connected_observer_;
    ObserverGuard klippy_observer_;
    ObserverGuard theme_observer_;
    bool gate_observers_wired_ = false;

    // Klippy's UDS path, from Moonraker's /server/config. Reachability is
    // probed once per activation, not per gate refresh: the gate recomputes on
    // every subject change and a connect() syscall each time would be waste.
    std::string klippy_socket_path_;
    bool klippy_socket_reachable_ = false;

    // Calibrator
    std::unique_ptr<helix::calibration::BeltTensionCalibrator> calibrator_;
    IMoonrakerAPI* api_ = nullptr;
    helix::IMoonrakerClient* client_ = nullptr;

    // Chart. One chart, re-parented between the RUNNING and RESULTS hosts.
    ui_frequency_response_chart_t* chart_ = nullptr;
    int series_[2] = {-1, -1};
    int ghost_series_[2] = {-1, -1};
    lv_obj_t* chart_host_running_ = nullptr;
    lv_obj_t* chart_host_results_ = nullptr;

    /// Render tier override for tests; nullopt reads PlatformCapabilities.
    std::optional<helix::PlatformTier> tier_override_;
    bool tier_animations_ = true;
    /// The printer's [resonance_tester] sweep; Klipper defaults until the query lands.
    helix::calibration::ResonanceTesterConfig sweep_cfg_;

    // Hardware detection cache. Feeds BeltGateInputs::is_corexy.
    helix::calibration::BeltTensionHardware detected_hw_;
    bool detection_pending_ = false; ///< detect_hardware() has not answered yet

    // Run state
    PathRun runs_[2];
    // The similarity of the latest comparison, and the one a single-path
    // re-test is measuring against: the re-tested path's note reads
    // " · was N%" from it.
    float similarity_percent_ = 0.0f;
    float was_similarity_percent_ = 0.0f;
    std::optional<helix::MemoryInfo> mem_override_;
    lv_obj_t* low_ram_dialog_ = nullptr;
    std::vector<helix::calibration::BeltPath> queue_;
    /// Size of the queue this run started with, for "2 of 2" detail lines.
    size_t run_queue_total_ = 0;
    /// True from begin_run() to cancel_run(): sweep callbacks that land after
    /// a cancel belong to a run this panel has already abandoned.
    bool run_active_ = false;
    uint32_t run_started_ms_ = 0;
    helix::ui::LvglTimerGuard elapsed_timer_;
    OperationTimeoutGuard stall_guard_;

    friend class BeltPanelFixture;
};

// Global instance accessor
inline BeltTensionPanel& get_global_belt_tension_panel() {
    return helix::lazy_global<BeltTensionPanel>("BeltTensionPanel");
}

/**
 * @brief Register XML event callbacks for belt tension panel
 *
 * Call once at startup before creating any panel_belt_tension XML.
 * Registers callbacks for all button events and initializes subjects.
 */
void ui_panel_belt_tension_register_callbacks();

/**
 * @brief Initialize row click callback for opening from Advanced panel
 *
 * Registers "on_belt_tension_row_clicked" callback.
 */
void init_belt_tension_row_handler();
