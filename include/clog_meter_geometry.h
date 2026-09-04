// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "lvgl/lvgl.h"

#include <cstdint>

namespace helix::ui {

/// The presentations `clog_meter_mode` selects between. Populated by
/// AmsState::update_clog_meter_subjects(); 0 means no detection backend is
/// available and the meter hides itself.
enum class ClogMeterMode : int {
    None = 0,
    Encoder = 1,   ///< 0..100 clog percentage, gradient safe -> clogged
    Flowguard = 2, ///< -100..+100, tangle at one end and clog at the other
    Buffer = 3,    ///< 0..100 AFC buffer fault proximity
};

/// Indicator colour as two design-token names plus a mix fraction, rather than
/// a resolved `lv_color_t`.
///
/// The rule is shared by the arc and the bar and is worth testing on its own,
/// but resolving a token needs a loaded theme. Naming the tokens keeps the
/// decision pure — the caller does `lv_color_mix(get(a), get(b), mix_a)`, which
/// is what LVGL's own argument order means: `mix_a` of 0 yields all `b`.
struct ClogMeterTint {
    const char* a;
    const char* b;
    uint8_t mix_a;
};

/// Which colour the indicator takes for a given mode/value/warning triple.
///
/// A warning is unconditional danger whatever the mode. Otherwise the linear
/// modes ramp primary -> warning -> danger across their 0..100 range, and
/// Flowguard stays primary: its extremes are already labelled at both ends of
/// the scale, so tinting the middle of a symmetrical range says nothing.
ClogMeterTint clog_meter_tint(int mode, int value, int warning);

/// clog_meter_tint() with its tokens resolved against the live theme, for the
/// two widgets that actually paint. Kept beside the rule so the arc and the bar
/// cannot drift into resolving it differently.
lv_color_t resolve_clog_tint(int mode, int value, int warning);

/// Severity of the current reading, as `clog_meter_status` carries it to the
/// status glyph. Ordered, so a renderer can compare rather than switch.
enum class ClogMeterStatus : int {
    Ok = 0,      ///< Below the danger threshold, or nothing to report
    Warning = 1, ///< At or past the threshold, but the backend has not tripped
    Fault = 2,   ///< The backend is reporting a warning: clogged, tangled, faulted
};

/// Which severity a mode/value/warning/threshold quadruple lands on.
///
/// The backend's own warning flag is authoritative — it is the thing that
/// pauses a print — so it always reads as a fault. Below that, a reading that
/// has reached the danger threshold is a warning even though nothing has
/// tripped yet, which is the whole point of showing a threshold at all.
/// `value` is compared by magnitude so Flowguard's tangle side counts.
ClogMeterStatus clog_meter_status(int mode, int value, int warning, int danger_pct);

/// Whether the reading means "nothing to report" rather than "zero danger".
///
/// AFC reports a buffer distance it is not currently tracking as zero, and a
/// distance at or beyond the fault threshold collapses to zero as well
/// (`BufferHealth::danger_value()`). Neither is a measurement, so both
/// renderers stand a check icon in place of the reading instead of drawing an
/// empty scale — which is what the bar did before it shared this predicate.
///
/// This is not the same as `ClogMeterMode::None`: mode 0 means there is no
/// detection hardware at all and the whole widget hides itself from XML.
bool clog_meter_is_safe(int mode, int value);

/// One coherent read of the five `clog_meter_*` subjects, plus everything the
/// renderers have to agree about.
///
/// The arc and the bar draw the same quantity in two shapes, and every time one
/// of them decided something for itself the two drifted: the arc treated an
/// untracked AFC buffer as "nothing to report" while the bar drew an empty
/// track, and each spelled "this mode is symmetrical" as its own `mode == 2`.
/// Anything both presentations must answer the same way belongs here.
struct ClogMeterSample {
    int mode = 0;
    int value = 0;
    int warning = 0;
    int danger_pct = 0;
    int peak_pct = 0;

    [[nodiscard]] ClogMeterMode kind() const {
        return static_cast<ClogMeterMode>(mode);
    }

    /// Nothing to report, as opposed to a measured zero. Both renderers stand
    /// a check icon in for this.
    [[nodiscard]] bool is_safe() const {
        return clog_meter_is_safe(mode, value);
    }

    /// The reading runs out from a centre rather than up from nothing, so the
    /// two ends mean opposite faults. The arc encodes this as an LVGL
    /// symmetrical range and the bar as centre-out geometry; the *decision* is
    /// this one.
    [[nodiscard]] bool is_symmetrical() const {
        return kind() == ClogMeterMode::Flowguard;
    }

    [[nodiscard]] ClogMeterStatus status() const {
        return clog_meter_status(mode, value, warning, danger_pct);
    }
};

/// Width of the value marker and the peak tick, in px. Both are deliberately
/// thin: the fill carries the reading, and these two only say "here" and
/// "worst so far". clog_bar_geometry() keeps both inside the track by this
/// width, and clog_bar_page.xml authors the same figure.
constexpr int kClogBarTickW = 2;

/// Pixel geometry of the horizontal FlowGuard bar, in track-local coordinates.
///
/// Every field is an x/width pair inside a track `track_w` px wide. A zero
/// width means "draw nothing" — the caller hides that piece rather than
/// drawing a degenerate rectangle.
struct ClogBarGeometry {
    int fill_x = 0;
    int fill_w = 0;
    /// Leading edge of the fill, where the value marker sits.
    int marker_x = 0;
    int peak_x = 0;
    /// Danger shading. Symmetrical modes shade both ends, linear modes only
    /// the far one, in which case `lo_w` is 0.
    int danger_lo_x = 0;
    int danger_lo_w = 0;
    int danger_hi_x = 0;
    int danger_hi_w = 0;
};

/// Lay the bar out for one sample.
///
/// `value` is the raw `clog_meter_value` — signed for Flowguard, 0..100
/// otherwise. `danger_pct` is the magnitude at which the reading is
/// dangerous, and `peak_pct` the worst magnitude seen this print. Both are
/// magnitudes even in the symmetrical mode, where the peak is drawn on the
/// side the current reading leans toward because the sample it came from
/// (max of |clog| and |tangle|) does not record which end produced it.
ClogBarGeometry clog_bar_geometry(int mode, int value, int danger_pct, int peak_pct, int track_w);

} // namespace helix::ui
