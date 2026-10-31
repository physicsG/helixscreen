// Copyright (C) 2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "plugin_self_heal.h"

namespace helix {

// Test-only access to the once-per-start latch of the plugin's startup check.
struct PluginSelfHealTestAccess {
    /// false: the next plugin report runs the check, as at app start.
    /// true: it never runs, so a test about something else cannot reach install.sh --relink.
    static void set_ran_this_start(bool ran) {
        PluginSelfHeal::ran_this_start_ = ran;
    }

    /// A pending restart waits for the screen, with its recheck timer armed.
    static bool restart_waiting_for_screen(const PluginSelfHeal& heal) {
        return heal.restart_wait_ == PluginTurnStep::WaitForScreen &&
               static_cast<bool>(heal.restart_timer_);
    }

    /// A pending restart waits for the job to let go.
    static bool restart_waiting_for_job(const PluginSelfHeal& heal) {
        return heal.restart_wait_ == PluginTurnStep::WaitForJob;
    }

    /// What the recheck timer runs on each tick.
    static void recheck_restart(PluginSelfHeal& heal) {
        heal.recheck_restart();
    }
};

} // namespace helix
