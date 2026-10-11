// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "env_whole_number.h"
#include "refresh_timing.h"

#include <spdlog/spdlog.h>

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <optional>

// Header-only: the DRM backend reads the HELIX_EGL_* switches from here, and that backend is
// also linked into binaries that do not carry DisplayManager.

namespace helix {

namespace refresh_timing_detail {

/// Whole milliseconds in [min_ms, max_ms], or 0 when `zero_means_off`. An unset variable reads
/// as nullopt; a set one that is not a plain decimal number it accepts reads as nullopt with a
/// warning.
inline std::optional<uint32_t> ms_from_env(const char* name, uint32_t min_ms, uint32_t max_ms,
                                           bool zero_means_off = false) {
    // "0" turns the pacing off, which the in-range parse below would reject.
    if (zero_means_off) {
        const char* value = std::getenv(name);
        if (value != nullptr && std::strcmp(value, "0") == 0) {
            return 0;
        }
    }
    return whole_number_from_env(name, min_ms, max_ms, "[RefreshTiming]", "whole milliseconds");
}

inline bool scope_all_from_env() {
    const char* value = std::getenv("HELIX_REFR_PERIOD_SCOPE");
    if (value == nullptr || std::strcmp(value, "display") == 0) {
        return false;
    }
    if (std::strcmp(value, "all") == 0) {
        return true;
    }
    spdlog::warn("[RefreshTiming] Ignoring HELIX_REFR_PERIOD_SCOPE='{}': expected display or all",
                 value);
    return false;
}

} // namespace refresh_timing_detail

/// Reads HELIX_REFR_PERIOD_MS, HELIX_REFR_PERIOD_SCOPE, HELIX_SCREENSAVER_REFR_PERIOD_MS and
/// HELIX_LOOP_MIN_SLEEP_MS. A rejected value keeps that field's default.
inline RefreshTiming refresh_timing_from_env() {
    using refresh_timing_detail::ms_from_env;
    RefreshTiming t;
    t.refr_period_ms = ms_from_env("HELIX_REFR_PERIOD_MS", RefreshTiming::MIN_PERIOD_MS,
                                   RefreshTiming::MAX_PERIOD_MS)
                           .value_or(0);
    t.scope_all = refresh_timing_detail::scope_all_from_env();
    t.screensaver_refr_period_ms =
        ms_from_env("HELIX_SCREENSAVER_REFR_PERIOD_MS", RefreshTiming::MIN_PERIOD_MS,
                    RefreshTiming::MAX_PERIOD_MS, true)
            .value_or(RefreshTiming::DEFAULT_SCREENSAVER_REFR_PERIOD_MS);
    // A set floor applies everywhere; unset, a running screensaver gets its own lower one.
    const std::optional<uint32_t> floor =
        ms_from_env("HELIX_LOOP_MIN_SLEEP_MS", RefreshTiming::MIN_LOOP_SLEEP_MS,
                    RefreshTiming::MAX_LOOP_SLEEP_MS);
    t.loop_min_sleep_ms = floor.value_or(RefreshTiming::DEFAULT_LOOP_MIN_SLEEP_MS);
    t.screensaver_loop_min_sleep_ms =
        floor.value_or(RefreshTiming::DEFAULT_SCREENSAVER_LOOP_MIN_SLEEP_MS);
    return t;
}

/// An EGL presentation switch, on by default: `0` turns it off, `1` or unset leaves it on, and
/// anything else leaves it on with a warning.
inline bool egl_switch_from_env(const char* name) {
    const char* value = std::getenv(name);
    if (value == nullptr || std::strcmp(value, "1") == 0) {
        return true;
    }
    if (std::strcmp(value, "0") == 0) {
        return false;
    }
    spdlog::warn("[RefreshTiming] Ignoring {}='{}': expected 0 or 1", name, value);
    return true;
}

/// HELIX_EGL_VSYNC: wait for each page flip.
inline bool egl_vsync_from_env() {
    return egl_switch_from_env("HELIX_EGL_VSYNC");
}

/// HELIX_EGL_PARTIAL_UPLOAD: send the display texture only the areas LVGL flushed.
inline bool egl_partial_upload_from_env() {
    return egl_switch_from_env("HELIX_EGL_PARTIAL_UPLOAD");
}

/// HELIX_EGL_XRGB: keep the display XRGB8888 and present it with the X byte ignored.
inline bool egl_xrgb_from_env() {
    return egl_switch_from_env("HELIX_EGL_XRGB");
}

/// What became of an EGL switch the environment may have asked for.
enum class EglSwitch {
    Off,      ///< turned off with 0; the driver keeps its default
    On,       ///< asked for, and the driver took it
    Declined, ///< asked for, and the driver refused it
};

/// Calls `turn_on` unless the switch `name` is 0; `turn_on` returns whether the driver took the
/// setting.
inline EglSwitch apply_egl_switch_from_env(const char* name, const std::function<bool()>& turn_on) {
    if (!egl_switch_from_env(name)) {
        return EglSwitch::Off;
    }
    return turn_on() ? EglSwitch::On : EglSwitch::Declined;
}

/// Calls `set_vsync(true)` unless HELIX_EGL_VSYNC is 0, and reports whether it did. With 0 the
/// driver keeps its own default.
inline bool apply_egl_vsync_from_env(const std::function<void(bool)>& set_vsync) {
    return apply_egl_switch_from_env("HELIX_EGL_VSYNC", [&set_vsync] {
               set_vsync(true);
               return true;
           }) == EglSwitch::On;
}

} // namespace helix
