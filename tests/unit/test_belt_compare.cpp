// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#include "belt_tension_types.h"
#include "shaper_csv_parser.h"

#include <cmath>
#include <fstream>

#include "../catch_amalgamated.hpp"

using namespace helix::calibration;

namespace {

/// Resolve tests/fixtures/ from __FILE__ so the test does not depend on cwd.
std::string fixture_dir() {
    std::string src = __FILE__;
    auto pos = src.rfind("/tests/unit/");
    if (pos != std::string::npos) {
        return src.substr(0, pos) + "/tests/fixtures/";
    }
    return "tests/fixtures/";
}

/// Real captures, one file per diagonal, named by axis: Path A is 1,-1 and
/// Path B is 1,1. Klipper writes bins every ~1.5 Hz well past the sweep
/// ceiling, and the two files' %.1f bins sit slightly off each other's grid:
/// the shape the band limit and interpolation exist for.
BeltCurve load_fixture(const std::string& name) {
    const std::string path = fixture_dir() + "belt_sweeps/" + name;
    auto data = parse_resonance_csv(path);
    INFO("fixture missing or unparsable: " << path);
    REQUIRE(data.error == ResonanceCsvError::NONE);
    REQUIRE_FALSE(data.curve.empty());
    return data.curve;
}

/// Compare a printer's captured pair over its own sweep band.
BeltComparison compare_fixture(const std::string& printer, float lo, float hi) {
    return compare_belt_paths(load_fixture(printer + "_axis_1_-1.csv"),
                              load_fixture(printer + "_axis_1_1.csv"), lo, hi);
}

/// Lorentzian peak on a noise floor, 5-135 Hz at Klipper's 3200/4096 Hz spacing.
BeltCurve curve_with_peak(float peak_hz, float height = 1e4f, float floor = 50.0f) {
    BeltCurve c;
    for (float f = 5.0f; f < 135.0f; f += 0.78125f) {
        const float d = (f - peak_hz) / 5.0f;
        c.emplace_back(f, floor + height / (1.0f + d * d));
    }
    return c;
}

BeltPeak peak(float hz, float amp) {
    return BeltPeak{hz, amp};
}

bool contains_freq(const std::vector<BeltPeak>& peaks, float hz) {
    for (const auto& p : peaks) {
        if (std::abs(p.freq_hz - hz) < 0.1f) {
            return true;
        }
    }
    return false;
}

} // namespace

// ============================================================================
// Real capture pair, band 5-135
// ============================================================================

static bool has_pair(const BeltComparison& cmp, float fa, float fb) {
    for (const auto& p : cmp.peaks.pairs) {
        if (std::abs(p.a.freq_hz - fa) < 0.1f && std::abs(p.b.freq_hz - fb) < 0.1f) {
            return true;
        }
    }
    return false;
}

// Expected values below were computed independently from the CSVs with numpy
// (np.interp onto the 1,-1 grid, np.corrcoef, np.percentile for the pairing
// threshold), each over the printer's own [resonance_tester] band.

TEST_CASE("real Voron 2.4 captures: similarity, pairs and unpaired peaks", "[belt][compare]") {
    // Uneven belts: the gantry is due for re-racking.
    const auto cmp = compare_fixture("voron24_kalico", 5.0f, 135.0f);
    REQUIRE(cmp.valid);
    CHECK(cmp.similarity_percent == Catch::Approx(48.3f).margin(1.0f));
    CHECK(cmp.verdict == BeltVerdict::ADJUST);

    REQUIRE(cmp.peaks.pairs.size() >= 3);
    CHECK(has_pair(cmp, 36.3f, 34.8f));
    CHECK(has_pair(cmp, 131.5f, 133.1f));
    CHECK(has_pair(cmp, 54.4f, 54.4f));

    // Strongest pair by amplitude sum: the tall ~35 Hz frame mode.
    CHECK(cmp.peaks.pairs.front().a.freq_hz == Catch::Approx(36.3f).margin(0.1f));
    CHECK(cmp.peaks.pairs.front().b.freq_hz == Catch::Approx(34.8f).margin(0.1f));

    // B's belt humps near 119.5 and 128.6 Hz have no A partner.
    CHECK(contains_freq(cmp.peaks.unpaired_b, 119.5f));
    CHECK(contains_freq(cmp.peaks.unpaired_b, 128.6f));
}

TEST_CASE("real K1C captures read well matched", "[belt][compare]") {
    const auto cmp = compare_fixture("k1c", 5.0f, 133.333f);
    REQUIRE(cmp.valid);
    CHECK(cmp.similarity_percent == Catch::Approx(98.9f).margin(1.0f));
    CHECK(cmp.verdict == BeltVerdict::MATCHED);
    REQUIRE_FALSE(cmp.peaks.pairs.empty());
    CHECK(cmp.peaks.pairs.front().a.freq_hz == Catch::Approx(58.7f).margin(0.1f));
    CHECK(cmp.peaks.pairs.front().b.freq_hz == Catch::Approx(58.6f).margin(0.1f));
    CHECK(contains_freq(cmp.peaks.unpaired_a, 63.3f));
    CHECK(contains_freq(cmp.peaks.unpaired_b, 84.9f));
}

TEST_CASE("real AD5M captures read well matched", "[belt][compare]") {
    const auto cmp = compare_fixture("ad5m", 5.0f, 100.0f);
    REQUIRE(cmp.valid);
    CHECK(cmp.similarity_percent == Catch::Approx(97.4f).margin(1.0f));
    CHECK(cmp.verdict == BeltVerdict::MATCHED);
    // One shared peak and nothing unpaired: a single distance, so the pairing
    // threshold is that distance itself.
    REQUIRE(cmp.peaks.pairs.size() == 1);
    CHECK(has_pair(cmp, 46.9f, 46.9f));
    CHECK(cmp.peaks.unpaired_a.empty());
    CHECK(cmp.peaks.unpaired_b.empty());
}

TEST_CASE("real K2 Plus captures read well matched", "[belt][compare]") {
    const auto cmp = compare_fixture("k2plus", 20.0f, 120.0f);
    REQUIRE(cmp.valid);
    CHECK(cmp.similarity_percent == Catch::Approx(92.2f).margin(1.0f));
    CHECK(cmp.verdict == BeltVerdict::MATCHED);
    CHECK(has_pair(cmp, 40.7f, 40.7f));
    CHECK(has_pair(cmp, 81.4f, 81.4f));
    CHECK(contains_freq(cmp.peaks.unpaired_a, 58.8f));
    CHECK(contains_freq(cmp.peaks.unpaired_a, 117.6f));
    CHECK(cmp.peaks.unpaired_b.empty());
}

TEST_CASE("real U1 captures read close", "[belt][compare]") {
    const auto cmp = compare_fixture("u1", 5.0f, 100.0f);
    REQUIRE(cmp.valid);
    CHECK(cmp.similarity_percent == Catch::Approx(81.7f).margin(1.0f));
    CHECK(cmp.verdict == BeltVerdict::CLOSE);
    // The main pair sits 3 Hz apart.
    REQUIRE_FALSE(cmp.peaks.pairs.empty());
    CHECK(cmp.peaks.pairs.front().a.freq_hz == Catch::Approx(49.9f).margin(0.1f));
    CHECK(cmp.peaks.pairs.front().b.freq_hz == Catch::Approx(46.8f).margin(0.1f));
    CHECK(has_pair(cmp, 54.6f, 57.7f));
    CHECK(cmp.peaks.unpaired_a.empty());
    CHECK(contains_freq(cmp.peaks.unpaired_b, 95.2f));
}

TEST_CASE("the band clips everything above the sweep ceiling", "[belt][compare]") {
    const auto a = load_fixture("voron24_kalico_axis_1_-1.csv");
    const auto b = load_fixture("voron24_kalico_axis_1_1.csv");
    constexpr float LO = 5.0f, HI = 135.0f;

    for (const auto* curve : {&a, &b}) {
        for (const auto& p : detect_belt_peaks(*curve, LO, HI)) {
            CHECK(p.freq_hz <= HI);
        }
    }
    const auto cmp = compare_belt_paths(a, b, LO, HI);
    REQUIRE(cmp.valid);
    for (const auto& pair : cmp.peaks.pairs) {
        CHECK(pair.a.freq_hz <= HI);
        CHECK(pair.b.freq_hz <= HI);
    }
    for (const auto& p : cmp.peaks.unpaired_a) {
        CHECK(p.freq_hz <= HI);
    }
    for (const auto& p : cmp.peaks.unpaired_b) {
        CHECK(p.freq_hz <= HI);
    }
}

// ============================================================================
// pair_belt_peaks()
// ============================================================================

TEST_CASE("identical peak lists pair one to one", "[belt][compare][pair]") {
    const std::vector<BeltPeak> peaks{peak(10.0f, 5.0f), peak(20.0f, 3.0f), peak(30.0f, 4.0f)};
    const auto r = pair_belt_peaks(peaks, peaks);

    REQUIRE(r.pairs.size() == 3);
    CHECK(r.unpaired_a.empty());
    CHECK(r.unpaired_b.empty());
    for (const auto& p : r.pairs) {
        CHECK(p.a.freq_hz == p.b.freq_hz);
    }
    // Distances [0,0,0,10,10,10,10,20,20]: median+1.5*IQR = 40, capped.
    CHECK(r.threshold_hz == Catch::Approx(belt_verdict::PAIR_MAX_HZ).margin(1e-4f));
}

TEST_CASE("a peak 11 Hz from its nearest partner stays unpaired", "[belt][compare][pair]") {
    const auto r = pair_belt_peaks({peak(50.0f, 1.0f)}, {peak(61.0f, 1.0f)});
    CHECK(r.pairs.empty());
    REQUIRE(r.unpaired_a.size() == 1);
    REQUIRE(r.unpaired_b.size() == 1);
    CHECK(r.unpaired_a[0].freq_hz == Catch::Approx(50.0f));
    CHECK(r.unpaired_b[0].freq_hz == Catch::Approx(61.0f));
    CHECK(r.threshold_hz == Catch::Approx(belt_verdict::PAIR_MAX_HZ).margin(1e-4f));
}

TEST_CASE("greedy pairing takes the globally closest pair first", "[belt][compare][pair]") {
    // Both a=10 (7 Hz) and a=15 (2 Hz) are within the threshold of b=17;
    // pairing in list order would take a=10 and strand a=15.
    const auto r = pair_belt_peaks({peak(10.0f, 1.0f), peak(15.0f, 1.0f)}, {peak(17.0f, 1.0f)});
    REQUIRE(r.pairs.size() == 1);
    CHECK(r.pairs[0].a.freq_hz == Catch::Approx(15.0f));
    CHECK(r.pairs[0].b.freq_hz == Catch::Approx(17.0f));
    REQUIRE(r.unpaired_a.size() == 1);
    CHECK(r.unpaired_a[0].freq_hz == Catch::Approx(10.0f));
}

TEST_CASE("empty peak lists pair nothing", "[belt][compare][pair]") {
    const auto both_empty = pair_belt_peaks({}, {});
    CHECK(both_empty.pairs.empty());
    CHECK(both_empty.unpaired_a.empty());
    CHECK(both_empty.unpaired_b.empty());
    CHECK(both_empty.threshold_hz == Catch::Approx(belt_verdict::PAIR_MAX_HZ).margin(1e-4f));

    const std::vector<BeltPeak> b{peak(10.0f, 1.0f)};
    const auto a_empty = pair_belt_peaks({}, b);
    CHECK(a_empty.pairs.empty());
    CHECK(a_empty.unpaired_a.empty());
    REQUIRE(a_empty.unpaired_b.size() == 1);
    CHECK(a_empty.threshold_hz == Catch::Approx(belt_verdict::PAIR_MAX_HZ).margin(1e-4f));
}

TEST_CASE("threshold is median + 1.5 * IQR when under the cap", "[belt][compare][pair]") {
    // All |fa - fb|: 0.4, 0.4, 0.6, 0.6, 1.6, 1.6 (sorted). Linear-interpolation
    // percentiles between closest ranks (numpy's default): p25 = 0.45,
    // median = 0.6, p75 = 1.35 -> threshold = 0.6 + 1.5 * 0.9 = 1.95.
    // The cap does not bind.
    const std::vector<BeltPeak> a{peak(10.0f, 1.0f), peak(11.0f, 1.0f), peak(12.0f, 1.0f)};
    const std::vector<BeltPeak> b{peak(10.4f, 1.0f), peak(11.6f, 1.0f)};
    const auto r = pair_belt_peaks(a, b);

    CHECK(r.threshold_hz == Catch::Approx(1.95f).margin(0.005f));
    REQUIRE(r.pairs.size() == 2);
    // Both 0.4 Hz distances tie; lower a index wins first.
    CHECK(r.pairs[0].a.freq_hz == Catch::Approx(10.0f));
    CHECK(r.pairs[0].b.freq_hz == Catch::Approx(10.4f));
    CHECK(r.pairs[1].a.freq_hz == Catch::Approx(12.0f));
    CHECK(r.pairs[1].b.freq_hz == Catch::Approx(11.6f));
    REQUIRE(r.unpaired_a.size() == 1);
    CHECK(r.unpaired_a[0].freq_hz == Catch::Approx(11.0f));
}

// ============================================================================
// detect_belt_peaks()
// ============================================================================

TEST_CASE("peaks are local maxima above a fraction of the band maximum", "[belt][compare]") {
    // 40 Hz is a local maximum (0.4 > 0.3, 0.4 >= 0.1) but sits below
    // 0.10 * 5.0, so only the 20 Hz peak survives.
    BeltCurve c{{10.0f, 0.2f}, {20.0f, 5.0f}, {30.0f, 0.3f},
                {40.0f, 0.4f}, {50.0f, 0.1f}, {60.0f, 0.2f}};
    const auto peaks = detect_belt_peaks(c, 10.0f, 60.0f);
    REQUIRE(peaks.size() == 1);
    CHECK(peaks[0].freq_hz == Catch::Approx(20.0f));
    CHECK(peaks[0].amplitude == Catch::Approx(5.0f));
}

TEST_CASE("a plateau counts as a peak on its left edge", "[belt][compare]") {
    // value > left neighbour, value >= right neighbour.
    BeltCurve c{{10.0f, 0.0f}, {20.0f, 5.0f}, {30.0f, 5.0f}, {40.0f, 0.0f}};
    const auto peaks = detect_belt_peaks(c, 10.0f, 40.0f);
    REQUIRE(peaks.size() == 1);
    CHECK(peaks[0].freq_hz == Catch::Approx(20.0f));
}

TEST_CASE("fewer than three in-band bins yields no peaks", "[belt][compare]") {
    BeltCurve c{{10.0f, 1.0f}, {20.0f, 5.0f}, {30.0f, 1.0f}, {40.0f, 9.0f}};
    CHECK(detect_belt_peaks(c, 10.0f, 20.0f).empty());
    CHECK(detect_belt_peaks({}, 5.0f, 135.0f).empty());
}

// ============================================================================
// band_similarity()
// ============================================================================

TEST_CASE("identical curves are 100 percent similar", "[belt][compare][similarity]") {
    const auto c = curve_with_peak(104.0f);
    CHECK(band_similarity(c, c, 5.0f, 135.0f) == Catch::Approx(100.0f).margin(0.01f));
}

TEST_CASE("similarity is scale invariant", "[belt][compare][similarity]") {
    const auto a = curve_with_peak(104.0f);
    BeltCurve b;
    b.reserve(a.size());
    for (const auto& [f, v] : a) {
        b.emplace_back(f, 2.0f * v);
    }
    CHECK(band_similarity(a, b, 5.0f, 135.0f) == Catch::Approx(100.0f).margin(0.01f));
}

TEST_CASE("anti-correlated curves clamp to zero", "[belt][compare][similarity]") {
    BeltCurve a, b;
    for (int i = 0; i <= 10; ++i) {
        const float f = 10.0f + 10.0f * i;
        a.emplace_back(f, static_cast<float>(i));
        b.emplace_back(f, 10.0f - static_cast<float>(i));
    }
    CHECK(band_similarity(a, b, 10.0f, 110.0f) == Catch::Approx(0.0f).margin(0.01f));
}

TEST_CASE("a curve on a different bin grid is interpolated", "[belt][compare][similarity]") {
    // Both curves are the same line (value = freq - 10); B is sampled at 20 Hz
    // spacing on a grid offset from A's. Linear interpolation of B onto A's
    // bins reproduces A exactly, so the correlation is 100. The real-capture
    // case above pins the interpolation arithmetic against np.interp numbers.
    const BeltCurve a{{10.0f, 0.0f}, {20.0f, 10.0f}, {30.0f, 20.0f}, {40.0f, 30.0f}};
    const BeltCurve b{{5.0f, -5.0f}, {25.0f, 15.0f}, {45.0f, 35.0f}};
    CHECK(band_similarity(a, b, 5.0f, 45.0f) == Catch::Approx(100.0f).margin(0.01f));
}

TEST_CASE("out-of-band bins are ignored", "[belt][compare][similarity]") {
    // Identical inside 20-30 Hz, wildly different outside.
    const BeltCurve a{{10.0f, 1e6f}, {20.0f, 5.0f}, {25.0f, 7.0f}, {30.0f, 5.0f}, {40.0f, 2e6f}};
    const BeltCurve b{{10.0f, 0.0f}, {20.0f, 5.0f}, {25.0f, 7.0f}, {30.0f, 5.0f}, {40.0f, 9e6f}};
    CHECK(band_similarity(a, b, 20.0f, 30.0f) == Catch::Approx(100.0f).margin(0.01f));
}

TEST_CASE("fewer than three in-band bins scores zero", "[belt][compare][similarity]") {
    const BeltCurve a{{10.0f, 1.0f}, {20.0f, 5.0f}};
    const BeltCurve b{{10.0f, 1.0f}, {20.0f, 5.0f}, {30.0f, 1.0f}};
    CHECK(band_similarity(a, b, 10.0f, 20.0f) == Catch::Approx(0.0f).margin(1e-6f));
    CHECK(band_similarity(b, a, 10.0f, 20.0f) == Catch::Approx(0.0f).margin(1e-6f));
}

TEST_CASE("zero variance scores zero", "[belt][compare][similarity]") {
    const BeltCurve flat{{10.0f, 4.0f}, {20.0f, 4.0f}, {30.0f, 4.0f}};
    const BeltCurve varied{{10.0f, 4.0f}, {20.0f, 9.0f}, {30.0f, 1.0f}};
    CHECK(band_similarity(flat, varied, 10.0f, 30.0f) == Catch::Approx(0.0f).margin(1e-6f));
    CHECK(band_similarity(flat, flat, 10.0f, 30.0f) == Catch::Approx(0.0f).margin(1e-6f));
}

// ============================================================================
// verdict_for_similarity()
// ============================================================================

TEST_CASE("verdict tier boundaries on similarity", "[belt][compare][verdict]") {
    CHECK(verdict_for_similarity(90.0f) == BeltVerdict::MATCHED);
    CHECK(verdict_for_similarity(100.0f) == BeltVerdict::MATCHED);
    CHECK(verdict_for_similarity(89.99f) == BeltVerdict::CLOSE);
    CHECK(verdict_for_similarity(75.0f) == BeltVerdict::CLOSE);
    CHECK(verdict_for_similarity(74.99f) == BeltVerdict::ADJUST);
    CHECK(verdict_for_similarity(0.0f) == BeltVerdict::ADJUST);
}

// ============================================================================
// compare_belt_paths()
// ============================================================================

TEST_CASE("well matched synthetic paths are MATCHED", "[belt][compare]") {
    const auto c = curve_with_peak(104.0f);
    const auto r = compare_belt_paths(c, c, 5.0f, 135.0f);
    REQUIRE(r.valid);
    CHECK(r.similarity_percent > 99.0f);
    CHECK(r.verdict == BeltVerdict::MATCHED);
    REQUIRE(r.peaks.pairs.size() == 1);
    CHECK(r.peaks.pairs[0].a.freq_hz == Catch::Approx(104.0f).margin(0.8f));
    CHECK(r.peaks.pairs[0].b.freq_hz == Catch::Approx(104.0f).margin(0.8f));
    CHECK(r.peaks.unpaired_a.empty());
    CHECK(r.peaks.unpaired_b.empty());
}

TEST_CASE("a curve with under three in-band bins is invalid, not a crash", "[belt][compare]") {
    BeltCurve low;
    for (float f = 5.0f; f < 6.5f; f += 0.78125f)
        low.emplace_back(f, 1000.0f);
    REQUIRE(low.size() < 3);
    auto r = compare_belt_paths(low, curve_with_peak(100.0f), 5.0f, 135.0f);
    CHECK_FALSE(r.valid);
    CHECK(r.verdict == BeltVerdict::ADJUST);
    r = compare_belt_paths({}, {}, 5.0f, 135.0f);
    CHECK_FALSE(r.valid);
}
