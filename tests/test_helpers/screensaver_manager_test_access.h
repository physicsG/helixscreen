// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "screensaver.h"

#ifdef HELIX_ENABLE_SCREENSAVER

#include "platform_capabilities.h"
#include "screensaver_base.h"
#include "screensaver_level_store.h"

#include <string>
#include <utility>

namespace helix {

// Test-only seam. The manager owns its saver instances, its gate state and its clock privately.
class ScreensaverManagerTestAccess {
  public:
    /// The saver the manager is running, or nullptr (also while it shows a black screen).
    static helix::ui::SaverBase* active(const ScreensaverManager& mgr) {
        return mgr.active_;
    }

    /// Replaces the CPU clock; the next idle-check tick samples it at once.
    static void set_cpu_clock(ScreensaverManager& mgr, helix::ui::CpuClockFn clock) {
        mgr.cpu_clock_ = std::move(clock);
        mgr.sampled_ = false;
    }

    static void reset_baseline(ScreensaverManager& mgr) {
        mgr.baseline_.reset();
        mgr.sampled_ = false;
    }

    static double baseline_rate(const ScreensaverManager& mgr) {
        return mgr.baseline_.rate();
    }

    static size_t baseline_samples(const ScreensaverManager& mgr) {
        return mgr.baseline_.size();
    }

    static bool showing_black_screen(const ScreensaverManager& mgr) {
        return mgr.black_screen_type_ != ScreensaverType::OFF;
    }

    static lv_obj_t* black_screen(const ScreensaverManager& mgr) {
        return mgr.black_screen_.obj();
    }

    /// The board fingerprint a saver started now would store.
    static std::string current_board(const ScreensaverManager& mgr) {
        return helix::ui::board_fingerprint(mgr.board_facts(helix::PlatformCapabilities::detect()));
    }
};

} // namespace helix

#endif // HELIX_ENABLE_SCREENSAVER
