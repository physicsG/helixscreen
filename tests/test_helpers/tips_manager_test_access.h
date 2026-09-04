// Copyright (C) 2025-2026 356C LLC
// tests/test_helpers/tips_manager_test_access.h
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "tips_manager.h"

#include <algorithm>
#include <string>

namespace helix {

/// Pins tip rotation so a layout measurement is reproducible.
///
/// TipsWidget renders `tip.title` from get_random_unique_tip(), and the
/// generator is seeded from std::random_device, so the string it measures is
/// different on every run. That was harmless while the database was only
/// loaded by Application::init_display() — the unit-test binary never called
/// it, so the cache was empty and the widget measured its own chrome. Loading
/// moved into get_instance(), which put real titles of every length in front
/// of the content-fit sweep and made it fail on a random set of geometries.
struct TipsManagerTestAccess {
    /// Reduce the database to its single longest title.
    ///
    /// The longest one rather than a fixed seed's pick: a tile that holds the
    /// worst case holds every other tip too, which is the property the gate is
    /// actually for. Returns the pinned title, empty if the database is
    /// unavailable — a caller that wants to assert it got pinned can check.
    static std::string pin_longest_title(TipsManager& mgr) {
        // Outside the lock: ensure_loaded() takes tips_mutex itself, and so
        // does the init() it calls.
        mgr.ensure_loaded();
        std::lock_guard<std::mutex> lock(mgr.tips_mutex);
        if (mgr.tips_cache.empty()) {
            return {};
        }
        auto longest = std::max_element(mgr.tips_cache.begin(), mgr.tips_cache.end(),
                                        [](const PrintingTip& a, const PrintingTip& b) {
                                            return a.title.size() < b.title.size();
                                        });
        PrintingTip pinned = *longest;
        mgr.tips_cache.assign(1, pinned);
        // get_random_unique_tip() skips ids it has already handed out and
        // resets the list when everything has been seen, so a stale entry from
        // an earlier test would make the one remaining tip look exhausted.
        mgr.viewed_tip_ids_.clear();
        return pinned.title;
    }
};

} // namespace helix
