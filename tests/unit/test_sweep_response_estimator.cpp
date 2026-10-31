// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#include "belt_test_signals.h"
#include "shaper_csv_parser.h"
#include "sweep_response_estimator.h"
#include "sweep_synth.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <vector>

#include "../catch_amalgamated.hpp"

using namespace helix::calibration;
using Catch::Approx;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();

std::vector<float> sine(size_t n, float amplitude, float freq_hz, float rate_hz,
                        float offset = 0.0f) {
    std::vector<float> out(n);
    for (size_t i = 0; i < n; ++i) {
        out[i] = offset + amplitude * static_cast<float>(std::sin(
                                          2.0 * kPi * freq_hz * static_cast<double>(i) / rate_hz));
    }
    return out;
}

/// The PSD a Hann-windowed sine of amplitude A reads at its own frequency:
/// A^2 (sum w)^2 / (2 rate sum w^2).
double hann_sine_psd(size_t n, double amplitude, double rate_hz) {
    double sum_w = 0.0;
    double sum_w2 = 0.0;
    for (size_t i = 0; i < n; ++i) {
        const double w =
            0.5 - 0.5 * std::cos(2.0 * kPi * static_cast<double>(i) / static_cast<double>(n - 1));
        sum_w += w;
        sum_w2 += w * w;
    }
    return amplitude * amplitude * sum_w * sum_w / (2.0 * rate_hz * sum_w2);
}

} // namespace

TEST_CASE("SweepPlan counts one point per Hz of a valid ramp", "[sweep_preview]") {
    const SweepPlan plan{5.0f, 135.0f, 1.0f};
    CHECK(plan.valid());
    CHECK(plan.point_count() == 131);
    CHECK(plan.point_hz(0) == 5.0f);
    CHECK(plan.point_hz(130) == 135.0f);

    const SweepPlan klipper{5.0f, 133.33f, 1.0f};
    CHECK(klipper.valid());
    CHECK(klipper.point_count() == 129);
    CHECK(klipper.point_hz(128) == 133.0f);
}

TEST_CASE("SweepPlan rejects degenerate ramps", "[sweep_preview]") {
    const SweepPlan bad[] = {
        {0.0f, 135.0f, 1.0f},
        {5.0f, 5.0f, 1.0f},
        {5.0f, 135.0f, 0.0f},
        {kNaN, 135.0f, 1.0f},
        {5.0f, std::numeric_limits<float>::infinity(), 1.0f},
    };
    for (const auto& plan : bad) {
        INFO(plan.freq_start_hz << " " << plan.freq_end_hz << " " << plan.hz_per_sec);
        CHECK_FALSE(plan.valid());
        CHECK(plan.point_count() == 0);
    }
}

TEST_CASE("SweepPoint columns match the CSV's", "[sweep_preview]") {
    const SweepPoint point{50.0f, 1.0f, 2.0f, 3.0f, true};
    CHECK(point.psd_xyz() == 6.0f);
    CHECK(sweep_column_value(point, SweepColumn::XYZ) == 6.0f);
    CHECK(sweep_column_value(point, SweepColumn::X) == 1.0f);
    CHECK(sweep_column_value(point, SweepColumn::Y) == 2.0f);

    const SweepPoint unclean{50.0f, 1.0f, 2.0f, 3.0f, false};
    CHECK(std::isnan(sweep_column_value(unclean, SweepColumn::XYZ)));
    CHECK(std::isnan(sweep_column_value(unclean, SweepColumn::X)));
    CHECK(std::isnan(sweep_column_value(unclean, SweepColumn::Y)));
}

TEST_CASE("the in-plane column sums x and y and ignores z", "[sweep_preview]") {
    const SweepPoint point{50.0f, 1.0f, 2.0f, 30.0f, true};
    CHECK(sweep_column_value(point, SweepColumn::IN_PLANE) == 3.0f);

    const SweepPoint unclean{50.0f, 1.0f, 2.0f, 30.0f, false};
    CHECK(std::isnan(sweep_column_value(unclean, SweepColumn::IN_PLANE)));
}

TEST_CASE("goertzel_psd reads a sine's PSD at its frequency", "[sweep_preview]") {
    const auto x = sine(3200, 1000.0f, 50.0f, 3200.0f);
    const double expected = hann_sine_psd(3200, 1000.0, 3200.0);
    CHECK(expected == Approx(333229.17).epsilon(1e-4));

    const float at = goertzel_psd(x.data(), x.size(), 50.0f, 3200.0f);
    CHECK(at == Approx(expected).epsilon(0.05));

    // 3.3 Hz away is far outside a 1 s Hann window's main lobe.
    const float below = goertzel_psd(x.data(), x.size(), 46.7f, 3200.0f);
    const float above = goertzel_psd(x.data(), x.size(), 53.3f, 3200.0f);
    CHECK(10.0 * std::log10(below / at) < -20.0);
    CHECK(10.0 * std::log10(above / at) < -20.0);

    const auto y = sine(1527, 500.0f, 40.0f, 3053.0f);
    const double expected_y = hann_sine_psd(1527, 500.0, 3053.0);
    CHECK(expected_y == Approx(41653.0).epsilon(1e-3));
    CHECK(goertzel_psd(y.data(), y.size(), 40.0f, 3053.0f) == Approx(expected_y).epsilon(0.05));
}

TEST_CASE("goertzel_psd removes the mean", "[sweep_preview]") {
    const auto plain = sine(3200, 1000.0f, 50.0f, 3200.0f);
    const auto gravity = sine(3200, 1000.0f, 50.0f, 3200.0f, 9810.0f);
    const float a = goertzel_psd(plain.data(), plain.size(), 50.0f, 3200.0f);
    const float b = goertzel_psd(gravity.data(), gravity.size(), 50.0f, 3200.0f);
    CHECK(b == Approx(a).epsilon(0.01));

    // A lock window is 0.5 s, so a low drive sits a few bins from DC, where
    // gravity's leakage through the window would swamp it.
    const auto low = sine(1527, 1000.0f, 5.0f, 3053.0f);
    const auto low_gravity = sine(1527, 1000.0f, 5.0f, 3053.0f, 9810.0f);
    const float c = goertzel_psd(low.data(), low.size(), 5.0f, 3053.0f);
    const float d = goertzel_psd(low_gravity.data(), low_gravity.size(), 5.0f, 3053.0f);
    CHECK(d == Approx(c).epsilon(0.01));
}

TEST_CASE("goertzel_psd and compute_psd read one windowed DFT at a bin", "[sweep_preview]") {
    // Both spectra read their bins off HannWindowedSignal and differ only in
    // scale: 2 |X|^2 / (rate sum w^2) against |X|^2 / (n rate).
    constexpr size_t kN = 2048;
    constexpr float kRate = 3200.0f;
    const auto x = sine(kN, 800.0f, 51.0f, kRate, 9810.0f);
    std::vector<AccelSample> samples(kN);
    for (size_t i = 0; i < kN; ++i) {
        samples[i] = {static_cast<float>(i) / kRate, x[i], 0.0f, 0.0f};
    }
    const auto belt = compute_psd(samples, kRate, 200.0f);
    const double sum_w2 = HannWindowedSignal(x.data(), kN).sum_w2();
    REQUIRE(sum_w2 > 0.0);

    for (size_t k : {20u, 33u, 60u}) {
        REQUIRE(k - 1 < belt.size());
        const float freq = static_cast<float>(k) * kRate / static_cast<float>(kN);
        const double preview = goertzel_psd(x.data(), kN, freq, kRate);
        REQUIRE(preview > 0.0);
        CHECK(belt[k - 1].second ==
              Approx(preview * sum_w2 / (2.0 * static_cast<double>(kN))).epsilon(1e-4));
    }
}

TEST_CASE("goertzel_psd is 0 for empty or meaningless input", "[sweep_preview]") {
    const std::vector<float> zeros(3200, 0.0f);
    CHECK(goertzel_psd(zeros.data(), zeros.size(), 50.0f, 3200.0f) == 0.0f);

    const auto x = sine(3200, 1000.0f, 50.0f, 3200.0f);
    CHECK(goertzel_psd(x.data(), 0, 50.0f, 3200.0f) == 0.0f);
    CHECK(goertzel_psd(x.data(), 1, 50.0f, 3200.0f) == 0.0f);
    CHECK(goertzel_psd(x.data(), x.size(), 50.0f, 0.0f) == 0.0f);
    CHECK(goertzel_psd(x.data(), x.size(), 0.0f, 3200.0f) == 0.0f);
    CHECK(goertzel_psd(x.data(), x.size(), -5.0f, 3200.0f) == 0.0f);
}

namespace {

/// A 0.5 s lock window at 3053 Hz: x carries gravity plus the tone, y the tone.
std::vector<AccelSample> tone_window(float freq_hz, float amplitude) {
    constexpr size_t n = 1527;
    constexpr float rate = 3053.0f;
    std::vector<AccelSample> out(n);
    for (size_t i = 0; i < n; ++i) {
        const float t = static_cast<float>(i) / rate;
        const auto s = static_cast<float>(amplitude * std::sin(2.0 * kPi * freq_hz * i / rate));
        out[i] = AccelSample{t, 9810.0f + s, s, 0.0f};
    }
    return out;
}

std::optional<float> drive_in(const std::vector<AccelSample>& w, float hint,
                              float hz_per_sec = 1.0f) {
    return find_drive_hz(w.data(), w.size(), 3053.0f, hint, hz_per_sec);
}

/// Feed one reading whose t0 estimate is `estimate`, at stream time `at`.
void observe_estimate(DriveLock& lock, double at, double estimate, const SweepPlan& plan) {
    lock.observe(at, static_cast<float>(plan.freq_start_hz + plan.hz_per_sec * (at - estimate)));
}

} // namespace

TEST_CASE("find_drive_hz finds the drive in a lock window", "[sweep_preview][lock]") {
    const auto w40 = tone_window(40.0f, 200.0f);
    auto f = drive_in(w40, 41.5f);
    REQUIRE(f.has_value());
    CHECK(*f == Approx(40.0f).margin(0.1));

    // Off the 0.25 Hz grid: the parabola refines it.
    f = drive_in(tone_window(40.3f, 200.0f), 41.5f);
    REQUIRE(f.has_value());
    CHECK(*f == Approx(40.3f).margin(0.1));
    f = drive_in(tone_window(40.12f, 200.0f), 41.5f);
    REQUIRE(f.has_value());
    CHECK(*f == Approx(40.12f).margin(0.05));

    // Grid 37..44 at 2 Hz/s.
    f = drive_in(tone_window(38.0f, 200.0f), 43.0f, 2.0f);
    REQUIRE(f.has_value());
    CHECK(*f == Approx(38.0f).margin(0.1));
}

TEST_CASE("find_drive_hz refuses what is not a drive inside the grid", "[sweep_preview][lock]") {
    const auto w40 = tone_window(40.0f, 200.0f);
    // Grid 42..45.5: the argmax sits on its low end.
    CHECK_FALSE(drive_in(w40, 45.0f).has_value());
    // Fewer than 3 meaningful grid points near DC.
    CHECK_FALSE(drive_in(w40, 0.3f).has_value());
    CHECK_FALSE(find_drive_hz(w40.data(), 0, 3053.0f, 41.5f, 1.0f).has_value());

    helix::calibration::test::Hiss rng;
    const auto hiss = helix::calibration::test::hiss_bed(1527, 30.0f, 3053.0f, rng);
    CHECK_FALSE(drive_in(hiss, 41.5f).has_value());
}

TEST_CASE("DriveLock locks once five estimates of t0 agree", "[sweep_preview][lock]") {
    const SweepPlan plan{5.0f, 135.0f, 1.0f};
    DriveLock lock(plan);
    CHECK(std::isnan(lock.drive_start_s()));
    CHECK(std::isnan(lock.model_hz(5.0)));

    lock.observe(2.00, 5.30f);
    lock.observe(2.25, 5.55f);
    lock.observe(2.50, 5.80f);
    lock.observe(2.75, 6.05f);
    CHECK_FALSE(lock.locked());
    lock.observe(3.00, 6.30f);
    REQUIRE(lock.locked());
    CHECK(lock.drive_start_s() == Approx(1.70).margin(1e-6));
    CHECK(lock.model_hz(10.0) == Approx(13.3f).margin(1e-4));
}

TEST_CASE("DriveLock ignores an outlier and spread estimates", "[sweep_preview][lock]") {
    const SweepPlan plan{5.0f, 135.0f, 1.0f};
    {
        DriveLock lock(plan);
        observe_estimate(lock, 2.00, 1.70, plan);
        observe_estimate(lock, 2.25, 1.70, plan);
        lock.observe(2.50, 4.30f); // estimate 3.20
        observe_estimate(lock, 2.75, 1.70, plan);
        observe_estimate(lock, 3.00, 1.70, plan);
        CHECK_FALSE(lock.locked());
        observe_estimate(lock, 3.25, 1.70, plan);
        REQUIRE(lock.locked());
        CHECK(lock.drive_start_s() == Approx(1.70).margin(1e-5));
    }
    {
        DriveLock lock(plan);
        for (int k = 0; k < 8; ++k) {
            observe_estimate(lock, 3.0 + 0.25 * k, 1.0 + 0.2 * k, plan);
        }
        CHECK_FALSE(lock.locked());
    }
}

TEST_CASE("DriveLock takes the median of the densest cluster", "[sweep_preview][lock]") {
    const SweepPlan plan{5.0f, 135.0f, 1.0f};
    DriveLock lock(plan);
    const double estimates[] = {1.60, 1.65, 1.70, 1.75, 1.80};
    for (int k = 0; k < 5; ++k) {
        observe_estimate(lock, 3.0 + 0.25 * k, estimates[k], plan);
    }
    REQUIRE(lock.locked());
    CHECK(lock.drive_start_s() == Approx(1.70).margin(1e-5));
}

TEST_CASE("DriveLock forgets estimates past its history", "[sweep_preview][lock]") {
    const SweepPlan plan{5.0f, 135.0f, 1.0f};
    DriveLock lock(plan);
    double at = 2.0;
    observe_estimate(lock, at, 1.70, plan);
    for (int k = 0; k < 16; ++k) {
        at += 0.25;
        observe_estimate(lock, at, 5.0 + 0.2 * k, plan);
    }
    for (int k = 0; k < 4; ++k) {
        at += 0.25;
        observe_estimate(lock, at, 1.70, plan);
    }
    CHECK_FALSE(lock.locked());
    at += 0.25;
    observe_estimate(lock, at, 1.70, plan);
    CHECK(lock.locked());
}

TEST_CASE("DriveLock drops a lock the readings disagree with", "[sweep_preview][lock]") {
    const SweepPlan plan{5.0f, 135.0f, 1.0f};
    auto locked_at_1_70 = [&plan]() {
        DriveLock lock(plan);
        for (int k = 0; k < 5; ++k) {
            observe_estimate(lock, 2.0 + 0.25 * k, 1.70, plan);
        }
        REQUIRE(lock.locked());
        return lock;
    };

    SECTION("three seconds of disagreement loses it") {
        auto lock = locked_at_1_70();
        double at = 10.0;
        for (; at <= 12.75 + 1e-9; at += 0.25) {
            lock.observe(at, lock.model_hz(at) + 2.0f);
        }
        CHECK(lock.locked());
        lock.observe(13.0, lock.model_hz(13.0) + 2.0f);
        CHECK_FALSE(lock.locked());
        CHECK(lock.drive_start_s() == Approx(1.70).margin(1e-5));
        CHECK(std::isnan(lock.model_hz(14.0)));

        // A fresh cluster relocks.
        for (int k = 0; k < 5; ++k) {
            observe_estimate(lock, 14.0 + 0.25 * k, 6.70, plan);
        }
        REQUIRE(lock.locked());
        CHECK(lock.drive_start_s() == Approx(6.70).margin(1e-5));
    }

    SECTION("one agreeing reading resets the timer") {
        auto lock = locked_at_1_70();
        for (double at = 10.0; at <= 12.75 + 1e-9; at += 0.25) {
            lock.observe(at, lock.model_hz(at) + 2.0f);
        }
        lock.observe(13.0, lock.model_hz(13.0));
        for (double at = 13.25; at <= 16.0 + 1e-9; at += 0.25) {
            lock.observe(at, lock.model_hz(at) + 2.0f);
        }
        CHECK(lock.locked());
    }
}

TEST_CASE("DriveLock counts a reading exactly LOCK_LOST_HZ off as agreeing",
          "[sweep_preview][lock]") {
    const SweepPlan plan{5.0f, 135.0f, 1.0f};
    DriveLock lock(plan);
    // Exact binary values: estimates of exactly 1.5.
    for (int k = 0; k < 5; ++k) {
        const double at = 2.0 + 0.25 * k;
        lock.observe(at, static_cast<float>(5.5 + 0.25 * k));
    }
    REQUIRE(lock.locked());
    REQUIRE(lock.drive_start_s() == 1.5);
    for (double at = 10.0; at <= 20.0; at += 0.25) {
        const float model = lock.model_hz(at);
        REQUIRE(model == static_cast<float>(5.0 + (at - 1.5)));
        lock.observe(at, model + sweep_preview::kLockLostHz);
    }
    CHECK(lock.locked());
}

TEST_CASE("DriveLock scales estimates by the sweep rate", "[sweep_preview][lock]") {
    const SweepPlan plan{5.0f, 135.0f, 2.0f};
    DriveLock lock(plan);
    for (int k = 0; k < 5; ++k) {
        lock.observe(3.0 + 0.25 * k, 7.6f + 0.5f * static_cast<float>(k));
    }
    REQUIRE(lock.locked());
    CHECK(lock.drive_start_s() == Approx(1.70).margin(1e-5));
}

namespace {

constexpr float kRate = 3053.0f;
constexpr size_t kBatch = 300;
constexpr double kConsoleLeadS = 1.5;

/// amp * sqrt(0.02 + 1 / (1 + ((f - peak) / hwhm)^2)) * weight[axis]
SweepAxisGain lorentzian(float peak_hz, float hwhm_hz,
                         std::array<float, 3> weights = {0.5f, 0.5f, 0.1f}, float amp = 800.0f) {
    return [=](float f) {
        const float d = (f - peak_hz) / hwhm_hz;
        const float level = amp * std::sqrt(0.02f + 1.0f / (1.0f + d * d));
        return std::array<float, 3>{level * weights[0], level * weights[1], level * weights[2]};
    };
}

SweepSynthSpec main_spec(SweepPlan plan = {5.0f, 135.0f, 1.0f}) {
    SweepSynthSpec spec;
    spec.plan = plan;
    spec.drive_start_s = 1.7;
    spec.rate_hz = kRate;
    spec.noise_mm_s2 = 30.0f;
    return spec;
}

/// What a case changes about the driver's stream.
struct DriveOptions {
    bool hints = true;
    /// Samples before this stream time are never delivered.
    double first_sample_s = 0.0;
    /// Batch [first, last] times -> whether it arrives contiguous.
    std::function<bool(double, double)> contiguous;
    /// Added to every sample before delivery.
    std::function<void(AccelSample&)> overlay;
    /// Rate each batch is delivered with, by its first sample's time.
    std::function<float(double)> rate;
    /// After each batch: the accumulator, the batch's last time, points so far.
    std::function<void(const SweepResponseAccumulator&, double, size_t)> after_batch;
    /// Swap to this synth (and its hints) from this stream time on.
    const SweepSynthSpec* second_spec = nullptr;
    double second_from_s = 0.0;
};

struct DriveResult {
    std::vector<SweepPoint> points;
    bool locked = false;
    double drive_start_s = 0.0;
    bool complete = false;
};

float ramp_hz(const SweepSynthSpec& spec, double t_s) {
    return static_cast<float>(spec.plan.freq_start_hz +
                              spec.plan.hz_per_sec * (t_s - spec.drive_start_s));
}

/// The console leads the motion by kConsoleLeadS and prints whole Hz.
DriveResult drive_sweep(const SweepSynthSpec& spec, const SweepAxisGain& gain,
                        const DriveOptions& opt = {}) {
    SweepResponseAccumulator acc(spec.plan);
    SweepSynth synth(spec, gain);
    std::unique_ptr<SweepSynth> second;
    if (opt.second_spec != nullptr) {
        // Advanced batch for batch with the first, so the swap keeps stream time.
        second = std::make_unique<SweepSynth>(*opt.second_spec, gain);
    }
    const SweepSynthSpec& last_spec = opt.second_spec != nullptr ? *opt.second_spec : spec;
    const double end_s =
        last_spec.drive_start_s +
        (last_spec.plan.freq_end_hz - last_spec.plan.freq_start_hz) / last_spec.plan.hz_per_sec +
        2.0;

    DriveResult result;
    while (synth.time_s() < end_s) {
        const double t = synth.time_s();
        const bool use_second = second && t >= opt.second_from_s;
        const SweepSynthSpec& active = use_second ? *opt.second_spec : spec;
        auto batch = synth.next(kBatch);
        if (second) {
            auto other = second->next(kBatch);
            if (use_second) {
                batch = std::move(other);
            }
        }
        if (static_cast<double>(batch.back().time) < opt.first_sample_s) {
            continue;
        }
        batch.erase(std::remove_if(batch.begin(), batch.end(),
                                   [&](const AccelSample& s) {
                                       return static_cast<double>(s.time) < opt.first_sample_s;
                                   }),
                    batch.end());
        if (opt.overlay) {
            for (auto& s : batch) {
                opt.overlay(s);
            }
        }
        if (opt.hints && t + kConsoleLeadS >= active.drive_start_s) {
            acc.note_console_frequency(
                std::floor(std::min(ramp_hz(active, t + kConsoleLeadS), active.plan.freq_end_hz)));
        }
        const bool contiguous =
            !opt.contiguous || opt.contiguous(static_cast<double>(batch.front().time),
                                              static_cast<double>(batch.back().time));
        const float rate = opt.rate ? opt.rate(t) : kRate;
        const auto fresh = acc.add_samples(batch, contiguous, rate);
        result.points.insert(result.points.end(), fresh.begin(), fresh.end());
        if (opt.after_batch) {
            opt.after_batch(acc, static_cast<double>(batch.back().time), result.points.size());
        }
    }
    result.locked = acc.locked();
    result.drive_start_s = acc.drive_start_s();
    result.complete = acc.complete();
    return result;
}

void require_every_point_once(const std::vector<SweepPoint>& points, const SweepPlan& plan) {
    REQUIRE(points.size() == plan.point_count());
    for (size_t i = 0; i < points.size(); ++i) {
        INFO("point " << i);
        REQUIRE(points[i].freq_hz == plan.point_hz(i));
    }
}

float argmax_hz(const std::vector<SweepPoint>& points) {
    float best_hz = 0.0f;
    float best = -1.0f;
    for (const auto& p : points) {
        if (p.clean && p.psd_xyz() > best) {
            best = p.psd_xyz();
            best_hz = p.freq_hz;
        }
    }
    return best_hz;
}

std::string estimator_fixture_dir() {
    std::string src = __FILE__;
    auto pos = src.rfind("/tests/unit/");
    if (pos != std::string::npos) {
        return src.substr(0, pos) + "/tests/fixtures/";
    }
    return "tests/fixtures/";
}

/// Linear interpolation of a CSV curve, clamped at its ends.
float interpolate(const BeltCurve& curve, float f) {
    if (f <= curve.front().first) {
        return curve.front().second;
    }
    for (size_t i = 1; i < curve.size(); ++i) {
        if (f <= curve[i].first) {
            const auto& [f0, p0] = curve[i - 1];
            const auto& [f1, p1] = curve[i];
            return p0 + (p1 - p0) * (f - f0) / (f1 - f0);
        }
    }
    return curve.back().second;
}

} // namespace

TEST_CASE("SweepResponseAccumulator measures a K1C-like sweep", "[sweep_preview][accumulator]") {
    const auto spec = main_spec();
    const auto r = drive_sweep(spec, lorentzian(58.4f, 3.8f));
    REQUIRE(r.locked);
    CHECK(std::abs(r.drive_start_s - 1.7) < 0.1);
    require_every_point_once(r.points, spec.plan);
    for (const auto& p : r.points) {
        INFO(p.freq_hz);
        CHECK(p.clean);
    }
    CHECK(std::abs(argmax_hz(r.points) - 58.4f) <= 1.0f);
    CHECK(r.complete);
}

TEST_CASE("SweepResponseAccumulator follows a 2 Hz/s sweep", "[sweep_preview][accumulator]") {
    const auto spec = main_spec({5.0f, 135.0f, 2.0f});
    const auto r = drive_sweep(spec, lorentzian(58.4f, 3.8f));
    REQUIRE(r.locked);
    CHECK(std::abs(r.drive_start_s - 1.7) < 0.1);
    require_every_point_once(r.points, spec.plan);
    CHECK(std::abs(argmax_hz(r.points) - 58.4f) <= 1.0f);
}

TEST_CASE("SweepResponseAccumulator is not captured by a ringing mode",
          "[sweep_preview][accumulator]") {
    const auto spec = main_spec({50.0f, 80.0f, 1.0f});
    DriveOptions opt;
    opt.overlay = [](AccelSample& s) {
        const auto ring = static_cast<float>(1000.0 * std::sin(2.0 * kPi * 52.0 * s.time));
        s.x += 0.6f * ring;
        s.y += 0.6f * ring;
        s.z += 0.1f * ring;
    };
    const auto r = drive_sweep(spec, lorentzian(58.4f, 3.8f), opt);
    REQUIRE(r.locked);
    CHECK(std::abs(r.drive_start_s - 1.7) < 0.1);
}

TEST_CASE("SweepResponseAccumulator keeps the per-axis columns apart",
          "[sweep_preview][accumulator]") {
    const auto spec = main_spec();
    const auto r = drive_sweep(spec, lorentzian(58.4f, 3.8f, {1.0f, 0.5f, 0.1f}));
    REQUIRE(r.locked);
    REQUIRE(r.points.size() == spec.plan.point_count());
    const SweepPoint* peak = nullptr;
    for (const auto& p : r.points) {
        if (!p.clean) {
            continue;
        }
        CHECK(p.psd_xyz() == p.psd_x + p.psd_y + p.psd_z);
        if (peak == nullptr || p.psd_xyz() > peak->psd_xyz()) {
            peak = &p;
        }
    }
    REQUIRE(peak != nullptr);
    CHECK(peak->psd_x > 3.0f * peak->psd_y);
}

TEST_CASE("SweepResponseAccumulator ignores a slow sweeping overlay",
          "[sweep_preview][accumulator]") {
    const auto spec = main_spec();
    const auto plain = drive_sweep(spec, lorentzian(58.4f, 3.8f));
    DriveOptions opt;
    opt.overlay = [](AccelSample& s) {
        s.x += static_cast<float>(400.0 * std::sin(2.0 * kPi * 0.83 * s.time));
    };
    const auto swept = drive_sweep(spec, lorentzian(58.4f, 3.8f), opt);
    REQUIRE(swept.locked);
    CHECK(std::abs(swept.drive_start_s - 1.7) < 0.15);
    REQUIRE(swept.points.size() == plain.points.size());
    for (size_t i = 0; i < swept.points.size(); ++i) {
        const auto& a = swept.points[i];
        const auto& b = plain.points[i];
        if (a.freq_hz <= 10.0f) {
            continue;
        }
        INFO(a.freq_hz);
        REQUIRE(a.clean);
        REQUIRE(b.clean);
        CHECK(std::abs(a.psd_xyz() - b.psd_xyz()) / b.psd_xyz() < 0.05f);
    }
}

TEST_CASE("SweepResponseAccumulator marks a step with a gap unclean",
          "[sweep_preview][accumulator]") {
    const auto spec = main_spec();
    const double gap_at = 1.7 + 25.0; // drive 30 Hz
    DriveOptions opt;
    opt.contiguous = [gap_at](double first, double last) {
        return !(first <= gap_at && gap_at <= last);
    };
    const auto r = drive_sweep(spec, lorentzian(58.4f, 3.8f), opt);
    require_every_point_once(r.points, spec.plan);
    for (const auto& p : r.points) {
        INFO(p.freq_hz);
        if (p.freq_hz == 30.0f) {
            CHECK_FALSE(p.clean);
            CHECK(std::isnan(p.psd_x));
            CHECK(std::isnan(p.psd_y));
            CHECK(std::isnan(p.psd_z));
        } else if (p.freq_hz <= 28.0f || p.freq_hz >= 33.0f) {
            CHECK(p.clean);
        }
    }
}

TEST_CASE("SweepResponseAccumulator never locks on hiss", "[sweep_preview][accumulator]") {
    for (uint32_t seed = 1; seed <= 5; ++seed) {
        INFO("seed " << seed);
        auto spec = main_spec();
        spec.seed = seed;
        bool ever_locked = false;
        DriveOptions opt;
        opt.after_batch = [&ever_locked](const SweepResponseAccumulator& acc, double, size_t) {
            ever_locked = ever_locked || acc.locked();
        };
        const auto r =
            drive_sweep(spec, [](float) { return std::array<float, 3>{0.0f, 0.0f, 0.0f}; }, opt);
        CHECK_FALSE(ever_locked);
        CHECK(r.points.empty());
    }
}

TEST_CASE("SweepResponseAccumulator takes no reading without a console hint",
          "[sweep_preview][accumulator]") {
    DriveOptions opt;
    opt.hints = false;
    const auto r = drive_sweep(main_spec(), lorentzian(58.4f, 3.8f), opt);
    CHECK_FALSE(r.locked);
    CHECK(r.points.empty());
}

TEST_CASE("SweepResponseAccumulator attaches after the drive started",
          "[sweep_preview][accumulator]") {
    const auto spec = main_spec();
    DriveOptions opt;
    opt.first_sample_s = 4.0;
    const auto r = drive_sweep(spec, lorentzian(58.4f, 3.8f), opt);
    REQUIRE(r.locked);
    require_every_point_once(r.points, spec.plan);
    for (const auto& p : r.points) {
        // The step's span start on the true ramp.
        const double span_start = 1.7 + (std::max(p.freq_hz - 0.5f, 5.0f) - 5.0f);
        INFO(p.freq_hz << " span starts " << span_start);
        CHECK(p.clean == (span_start >= 4.0));
    }
}

TEST_CASE("SweepResponseAccumulator drops and regains a lock", "[sweep_preview][accumulator]") {
    const auto spec = main_spec();
    auto later = main_spec();
    later.drive_start_s = 7.7; // the ramp drops 6 Hz at 30 s
    bool lost_after_33 = false;
    bool relocked_after_loss = false;
    DriveOptions opt;
    opt.second_spec = &later;
    opt.second_from_s = 30.0;
    opt.after_batch = [&](const SweepResponseAccumulator& acc, double t, size_t) {
        if (t > 33.0 && !acc.locked()) {
            lost_after_33 = true;
        }
        if (lost_after_33 && acc.locked()) {
            relocked_after_loss = true;
        }
    };
    const auto r = drive_sweep(spec, lorentzian(58.4f, 3.8f), opt);
    CHECK(lost_after_33);
    CHECK(relocked_after_loss);
    REQUIRE(r.locked);
    CHECK(std::abs(r.drive_start_s - 7.7) < 0.1);
    require_every_point_once(r.points, spec.plan);
    CHECK(std::any_of(r.points.begin(), r.points.end(),
                      [](const SweepPoint& p) { return !p.clean; }));
}

TEST_CASE("SweepResponseAccumulator takes no reading at rate 0", "[sweep_preview][accumulator]") {
    const auto spec = main_spec();
    bool locked_during_rate_0 = false;
    size_t points_during_rate_0 = 0;
    DriveOptions opt;
    // Batches starting before 6 s arrive with rate 0.
    opt.rate = [](double t) { return t < 6.0 ? 0.0f : kRate; };
    opt.after_batch = [&](const SweepResponseAccumulator& acc, double t, size_t points) {
        if (t < 6.0) {
            locked_during_rate_0 = locked_during_rate_0 || acc.locked();
            points_during_rate_0 = points;
        }
    };
    const auto r = drive_sweep(spec, lorentzian(58.4f, 3.8f), opt);
    CHECK_FALSE(locked_during_rate_0);
    CHECK(points_during_rate_0 == 0);
    REQUIRE(r.locked);
    require_every_point_once(r.points, spec.plan);
}

TEST_CASE("SweepResponseAccumulator reproduces real CSV shapes", "[sweep_preview][accumulator]") {
    const char* fixtures[] = {"k1c_axis_1_-1.csv", "k1c_axis_1_1.csv",
                              "voron24_kalico_axis_1_1.csv", "ad5m_axis_1_1.csv"};
    for (const char* name : fixtures) {
        INFO(name);
        const auto csv = parse_resonance_csv(estimator_fixture_dir() + "belt_sweeps/" + name);
        REQUIRE(csv.error == ResonanceCsvError::NONE);
        REQUIRE_FALSE(csv.curve.empty());
        const BeltCurve curve = csv.curve;
        const SweepAxisGain gain = [curve](float f) {
            const float p = std::max(interpolate(curve, f), 0.0f);
            return std::array<float, 3>{20.0f * std::sqrt(0.45f * p), 20.0f * std::sqrt(0.45f * p),
                                        20.0f * std::sqrt(0.10f * p)};
        };
        const auto r = drive_sweep(main_spec(), gain);
        REQUIRE(r.locked);
        BeltCurve preview;
        for (const auto& p : r.points) {
            if (p.clean) {
                preview.emplace_back(p.freq_hz, p.psd_xyz());
            }
        }
        const float similarity = band_similarity(csv.curve, preview, 5.0f, 135.0f);
        INFO("similarity " << similarity);
        CHECK(similarity >= 90.0f);
    }
}
