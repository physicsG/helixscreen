// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ui_update_queue.h"

#include "printer_state.h"
#include "update_queue_test_access.h"

namespace helix {

class PrinterPrintStateTestAccess {
  public:
    static void reset_extra(PrinterPrintState& pps) {
        pps.estimated_print_time_ = 0;
        // File-scoped slice geometry + per-print Z cache used by the Z-height
        // layer derivation. Like estimated_print_time_ these survive
        // reset_for_new_print() in production; the test reset() simulates a fresh
        // session on the shared singleton, so clear them for cross-test isolation.
        pps.layer_height_ = 0.0;
        pps.first_layer_height_ = 0.0;
        pps.last_gcode_z_mm_ = 0.0;
        pps.have_gcode_z_ = false;
        pps.layer_z_derived_ = false;
        pps.has_real_layer_data_ = false;
        // Sticky printer capability — session-scoped in production (survives
        // reset_for_new_print, cleared only on a fresh session). The test
        // reset() simulates a fresh session on the shared PrinterState
        // singleton, so clear it here for cross-test isolation.
        pps.printer_reports_layers_ = false;
        pps.slicer_progress_ = 0.0;
        pps.slicer_progress_active_ = false;
        pps.smoothed_remaining_ = 0.0;
        pps.has_smoothed_remaining_ = false;
        pps.sdcard_active_ = false;
    }

    /// Mark the layer counters as coming from real slicer/Moonraker fields
    /// rather than being derived from the progress fraction.
    static void set_has_real_layer_data(PrinterPrintState& pps, bool value) {
        pps.has_real_layer_data_ = value;
    }
};

// PrinterStateTestAccess must be in namespace helix to match friend declaration in PrinterState
class PrinterStateTestAccess {
  public:
    static void reset(PrinterState& ps) {
        helix::ui::UpdateQueueTestAccess::drain(helix::ui::UpdateQueue::instance());
        // Drop the discovered fan list before the subjects go away. init_fans()
        // carries live readings across a re-init for fans that persist (#1181),
        // so a leaked fans_ now leaks speed_percent/ever_ran/rpm into the next
        // test instead of being silently zeroed. Re-initing with an empty list is
        // the public way to clear the list and expire every per-fan subject.
        ps.fan_state_.init_fans({});
        ps.deinit_subjects();
        ps.printer_type_.clear();
        ps.pre_print_option_set_ = PrePrintOptionSet();
        ps.z_offset_calibration_strategy_ = ZOffsetCalibrationStrategy::PROBE_CALIBRATE;
        ps.auto_detected_bed_moves_ = false;
        ps.is_paused_ = false;
        ps.last_kinematics_.clear();
        PrinterPrintStateTestAccess::reset_extra(ps.print_domain_);
    }

    static PrinterFanState& get_fan_state(PrinterState& ps) {
        return ps.fan_state_;
    }

    static PrinterPrintState& get_print_state(PrinterState& ps) {
        return ps.print_domain_;
    }

    /// Inject a synthetic pre-print option set (bypasses the printer DB) so tests
    /// can exercise option configurations that no shipped printer declares yet —
    /// e.g. a bed_mesh option with a custom adaptive_param name.
    static void set_option_set(PrinterState& ps, PrePrintOptionSet set) {
        ps.pre_print_option_set_ = std::move(set);
    }
};

} // namespace helix

using namespace helix;
