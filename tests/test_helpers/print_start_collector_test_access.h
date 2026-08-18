// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "print_start_collector.h"

class PrintStartCollectorTestAccess {
  public:
    /// Wind back the start time to simulate elapsed seconds.
    ///
    /// Also winds back last_activity_time_, so this models "N seconds passed
    /// and the printer said nothing" — the case the timeouts exist for. Use
    /// set_last_activity_seconds_ago() afterwards to model a printer that is
    /// still narrating its pre-print.
    static void set_elapsed_seconds(PrintStartCollector& c, int seconds) {
        std::lock_guard<std::mutex> lock(c.state_mutex_);
        auto when = std::chrono::steady_clock::now() - std::chrono::seconds(seconds);
        c.printing_state_start_ = when;
        c.last_activity_time_ = when;
    }

    /// Set how long ago the last pre-print activity (phase match or probe line)
    /// was observed, independent of total elapsed time.
    static void set_last_activity_seconds_ago(PrintStartCollector& c, int seconds) {
        std::lock_guard<std::mutex> lock(c.state_mutex_);
        c.last_activity_time_ = std::chrono::steady_clock::now() - std::chrono::seconds(seconds);
    }

    /// Set predicted_total_seconds_ directly for timeout threshold tests
    static void set_predicted_total(PrintStartCollector& c, float seconds) {
        std::lock_guard<std::mutex> lock(c.state_mutex_);
        c.predicted_total_seconds_ = seconds;
    }

    /// Read predicted_total_seconds_
    static float get_predicted_total(PrintStartCollector& c) {
        std::lock_guard<std::mutex> lock(c.state_mutex_);
        return c.predicted_total_seconds_;
    }

    /// Wind back the silent-progression "temps ready" timestamp to simulate
    /// elapsed seconds since temps became ready. The collector sets this on
    /// the first tick where bed+nozzle reach target — bypassing the natural
    /// path keeps tests deterministic.
    static void set_temps_ready_elapsed_seconds(PrintStartCollector& c, int seconds) {
        c.temps_ready_time_ = std::chrono::steady_clock::now() - std::chrono::seconds(seconds);
    }

    /// Reset silent_progression_idx_ to N (default 0) so a test can rewind
    /// after a fired entry.
    static void set_silent_progression_idx(PrintStartCollector& c, size_t idx) {
        c.silent_progression_idx_ = idx;
    }

    /// Read mesh_probe_total_ — used to verify the "Adapted probe count"
    /// gcode parser updates the live denominator.
    static int get_mesh_probe_total(PrintStartCollector& c) {
        std::lock_guard<std::mutex> lock(c.state_mutex_);
        return c.mesh_probe_total_;
    }

    /// Directly invoke the private CAS-guarded heating relabel. In production
    /// check_fallback_completion() calls this with a phase snapshot that a
    /// concurrent bg gcode signal may have invalidated; a test uses this to
    /// prove the guard refuses when current_phase_ has advanced past heating.
    static void relabel_heating_phase(PrintStartCollector& c, helix::PrintStartPhase resolved) {
        c.relabel_heating_phase(resolved);
    }
};
