// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_shaper_selection.cpp
 * @brief Which shaper a chart chip selection refers to (pure rule, no LVGL)
 *
 * `InputShaperResult::all_shapers` is scraped from Klipper's SHAPER_CALIBRATE
 * console output; `ShaperResponseCurve` comes from the calibration CSV. They are
 * independent vectors with no guaranteed length or ordering relationship, so the
 * join has to be by shaper name, case-insensitively. These tests pin that rule -
 * especially the mismatched-order case, where a positional lookup would apply
 * the wrong shaper to the user's printer config.
 */

#include "shaper_selection.h"

#include "../catch_amalgamated.hpp"

using helix::calibration::resolve_selected_shaper;
using helix::calibration::SelectedShaper;

namespace {

ShaperOption option(const char* type, float freq, float vib, float accel) {
    ShaperOption o;
    o.type = type;
    o.frequency = freq;
    o.vibrations = vib;
    o.max_accel = accel;
    return o;
}

ShaperResponseCurve curve(const char* name, float freq) {
    ShaperResponseCurve c;
    c.name = name;
    c.frequency = freq;
    return c;
}

/// Recommendation: mzv @ 42.4 Hz, 1.5% vibrations, 3200 mm/s^2.
InputShaperResult make_result() {
    InputShaperResult r;
    r.axis = 'X';
    r.shaper_type = "mzv";
    r.shaper_freq = 42.4f;
    r.vibrations = 1.5f;
    r.max_accel = 3200.0f;
    r.all_shapers = {
        option("zv", 38.2f, 4.7f, 5100.0f),
        option("mzv", 42.4f, 1.5f, 3200.0f),
        option("ei", 53.8f, 0.9f, 2600.0f),
    };
    return r;
}

} // namespace

// ============================================================================
// No selection - the firmware recommendation
// ============================================================================

TEST_CASE("No selection falls back to the recommendation", "[input_shaper][selection]") {
    const InputShaperResult res = make_result();
    const std::vector<ShaperResponseCurve> curves = {curve("zv", 38.0f), curve("mzv", 42.0f)};

    const SelectedShaper sel = resolve_selected_shaper(res, curves, -1);

    CHECK(sel.type == "mzv");
    CHECK(sel.frequency == Catch::Approx(42.4f));
    CHECK(sel.vibrations == Catch::Approx(1.5f));
    CHECK(sel.max_accel == Catch::Approx(3200.0f));
    CHECK_FALSE(sel.from_selection);
    CHECK(sel.metrics_known);
    CHECK(sel.is_valid());
}

TEST_CASE("Out-of-range selection falls back to the recommendation", "[input_shaper][selection]") {
    const InputShaperResult res = make_result();

    SECTION("index past the end of a populated curve list") {
        const std::vector<ShaperResponseCurve> curves = {curve("zv", 38.0f), curve("ei", 53.0f)};
        const SelectedShaper sel = resolve_selected_shaper(res, curves, 2);

        CHECK(sel.type == "mzv");
        CHECK(sel.frequency == Catch::Approx(42.4f));
        CHECK(sel.vibrations == Catch::Approx(1.5f));
        CHECK(sel.max_accel == Catch::Approx(3200.0f));
        CHECK_FALSE(sel.from_selection);
        CHECK(sel.metrics_known);
    }

    SECTION("index 0 with no curves at all - the CSV never loaded") {
        const std::vector<ShaperResponseCurve> curves;
        const SelectedShaper sel = resolve_selected_shaper(res, curves, 0);

        CHECK(sel.type == "mzv");
        CHECK(sel.frequency == Catch::Approx(42.4f));
        CHECK_FALSE(sel.from_selection);
        CHECK(sel.metrics_known);
    }
}

TEST_CASE("An empty recommendation with no selection is not valid", "[input_shaper][selection]") {
    InputShaperResult res;
    res.shaper_type.clear();
    res.shaper_freq = 0.0f;

    const SelectedShaper sel = resolve_selected_shaper(res, {}, -1);

    CHECK(sel.type.empty());
    CHECK(sel.frequency == Catch::Approx(0.0f));
    CHECK_FALSE(sel.is_valid());
    CHECK_FALSE(sel.from_selection);
}

// ============================================================================
// Selection joined to the console list
// ============================================================================

TEST_CASE("Selecting a curve with a console counterpart uses the console metrics",
          "[input_shaper][selection]") {
    const InputShaperResult res = make_result();
    const std::vector<ShaperResponseCurve> curves = {curve("zv", 38.0f), curve("mzv", 42.0f),
                                                     curve("ei", 53.0f)};

    const SelectedShaper sel = resolve_selected_shaper(res, curves, 0);

    CHECK(sel.type == "zv");
    CHECK(sel.frequency == Catch::Approx(38.2f));
    CHECK(sel.vibrations == Catch::Approx(4.7f));
    CHECK(sel.max_accel == Catch::Approx(5100.0f));
    CHECK(sel.from_selection);
    CHECK(sel.metrics_known);
    CHECK(sel.is_valid());
}

TEST_CASE("The console frequency wins over the rounded CSV header frequency",
          "[input_shaper][selection]") {
    // The CSV header rounds; the console prints the actual fit. Applying the
    // rounded number would write a subtly wrong printer.cfg.
    const InputShaperResult res = make_result();
    const std::vector<ShaperResponseCurve> curves = {curve("ei", 53.0f)};

    const SelectedShaper sel = resolve_selected_shaper(res, curves, 0);

    CHECK(sel.type == "ei");
    CHECK(sel.frequency == Catch::Approx(53.8f));
    CHECK(sel.frequency != Catch::Approx(53.0f));
    CHECK(sel.metrics_known);
}

TEST_CASE("The name join is case-insensitive", "[input_shaper][selection]") {
    // The CSV column names and the console output are produced by different
    // halves of Klipper and do not agree on case.
    const InputShaperResult res = make_result();
    const std::vector<ShaperResponseCurve> curves = {curve("MZV", 42.0f)};

    const SelectedShaper sel = resolve_selected_shaper(res, curves, 0);

    CHECK(sel.type == "mzv"); // the console's spelling, not the CSV's
    CHECK(sel.frequency == Catch::Approx(42.4f));
    CHECK(sel.vibrations == Catch::Approx(1.5f));
    CHECK(sel.max_accel == Catch::Approx(3200.0f));
    CHECK(sel.from_selection);
    CHECK(sel.metrics_known);
}

// ============================================================================
// Selection with no console counterpart
// ============================================================================

TEST_CASE("A curve absent from the console list reports unknown metrics",
          "[input_shaper][selection]") {
    const InputShaperResult res = make_result(); // zv / mzv / ei only
    const std::vector<ShaperResponseCurve> curves = {curve("3hump_ei", 61.5f)};

    const SelectedShaper sel = resolve_selected_shaper(res, curves, 0);

    CHECK(sel.type == "3hump_ei");
    CHECK(sel.frequency == Catch::Approx(61.5f));
    CHECK(sel.vibrations == Catch::Approx(0.0f));
    CHECK(sel.max_accel == Catch::Approx(0.0f));
    CHECK(sel.from_selection);
    CHECK_FALSE(sel.metrics_known); // the caller must blank, not print 0%
    CHECK(sel.is_valid());          // still applyable - type and freq are known
}

// ============================================================================
// The regression that matters: different lengths, different orders
// ============================================================================

TEST_CASE("Selection resolves by name, never by position", "[input_shaper][selection]") {
    // Five console fits in Klipper's print order; three CSV curves in a
    // deliberately different order and count. A positional implementation
    // resolves index 0 -> "zv" and index 2 -> "ei"; the correct answers are
    // "3hump_ei" and "mzv".
    InputShaperResult res;
    res.shaper_type = "mzv";
    res.shaper_freq = 44.0f;
    res.vibrations = 2.0f;
    res.max_accel = 3000.0f;
    res.all_shapers = {
        option("zv", 30.0f, 9.0f, 7000.0f),       option("mzv", 44.0f, 2.0f, 3000.0f),
        option("ei", 50.0f, 1.0f, 2500.0f),       option("2hump_ei", 58.0f, 0.5f, 2000.0f),
        option("3hump_ei", 66.0f, 0.2f, 1500.0f),
    };

    const std::vector<ShaperResponseCurve> curves = {
        curve("3hump_ei", 66.0f),
        curve("ei", 50.0f),
        curve("mzv", 44.0f),
    };

    SECTION("index 0 -> 3hump_ei, not the first console entry") {
        const SelectedShaper sel = resolve_selected_shaper(res, curves, 0);
        CHECK(sel.type == "3hump_ei");
        CHECK(sel.frequency == Catch::Approx(66.0f));
        CHECK(sel.vibrations == Catch::Approx(0.2f));
        CHECK(sel.max_accel == Catch::Approx(1500.0f));
        CHECK(sel.from_selection);
        CHECK(sel.metrics_known);
    }

    SECTION("index 2 -> mzv, not the third console entry") {
        const SelectedShaper sel = resolve_selected_shaper(res, curves, 2);
        CHECK(sel.type == "mzv");
        CHECK(sel.frequency == Catch::Approx(44.0f));
        CHECK(sel.vibrations == Catch::Approx(2.0f));
        CHECK(sel.max_accel == Catch::Approx(3000.0f));
        CHECK(sel.from_selection);
        CHECK(sel.metrics_known);
    }

    SECTION("index 3 is past the CSV list even though the console has 5 entries") {
        const SelectedShaper sel = resolve_selected_shaper(res, curves, 3);
        CHECK(sel.type == "mzv");
        CHECK(sel.frequency == Catch::Approx(44.0f));
        CHECK_FALSE(sel.from_selection);
    }
}
