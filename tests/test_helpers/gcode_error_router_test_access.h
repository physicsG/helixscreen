// Copyright (C) 2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "gcode_error_router.h"

#include <string>

// Test-only friend that exposes GcodeErrorRouter's private presentation glue.
// process_line() is the seam between the (already unit-tested) pure routing
// decision and the LVGL/notification surfacing; reaching it directly lets the
// e2e test drive a raw `!!` line straight to a modal without standing up a
// MoonrakerClient + WebSocket. See gcode_error_router.h (friend declaration).
struct GcodeErrorRouterTestAccess {
    static void process_line(helix::GcodeErrorRouter& r, const std::string& line) {
        r.process_line(line);
    }

    /// Drives the notify_status_update body exactly as the bg_cb-deferred
    /// delivery lands it on main.
    static void on_notify_status_update(helix::GcodeErrorRouter& r, const nlohmann::json& msg) {
        r.on_notify_status_update(msg);
    }

    /// Drives a fault raise-notification body exactly as the bg_cb-deferred
    /// delivery lands it on main.
    static void on_notify_fault(helix::GcodeErrorRouter& r, const std::string& method,
                                const nlohmann::json& msg) {
        r.on_notify_fault(method, msg);
    }

    /// Held presentations that actually fired, and which fault's spelling
    /// showed last (a coded event names its code, a prose event its text).
    static size_t deferred_shown_count(const helix::GcodeErrorRouter& r) {
        return r.deferred_shown_count_;
    }

    static const std::string& last_deferred_shown(const helix::GcodeErrorRouter& r) {
        return r.last_deferred_shown_;
    }

    /// Fires the connect observer's body (standing-fault reset + gcode_store
    /// replay; the replay is a no-op with a null client).
    static void on_connected(helix::GcodeErrorRouter& r) {
        r.on_connected();
    }

    /// Standing-fault lines handed to process_line so far. The correlation
    /// registry's short window would make "did not surface twice" unfalsifiable,
    /// so tests assert here.
    static size_t standing_fed_count(const helix::GcodeErrorRouter& r) {
        return r.standing_fed_count_;
    }

    /// Size of the standing-fault seen set: what a later frame diffs against.
    static size_t standing_seen_count(const helix::GcodeErrorRouter& r) {
        return r.standing_fault_lines_.size();
    }
};
