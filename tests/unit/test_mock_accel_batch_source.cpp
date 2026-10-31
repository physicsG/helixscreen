// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#include "accel_batch_source_mock.h"
#include "belt_stream_client.h"
#include "sweep_response_estimator.h"

#include <chrono>
#include <cmath>
#include <mutex>
#include <thread>
#include <vector>

#include "../catch_amalgamated.hpp"

using namespace helix::calibration;
using Catch::Approx;

TEST_CASE("mock_sweep_axis_gain is a Lorentzian on a floor", "[mock][sweep_preview]") {
    const MockSweepResponse r{58.4f, 5.0f, 300.0f, {0.671f, 0.671f, 0.316f}};
    const auto at_peak = mock_sweep_axis_gain(r, 58.4f);
    const auto off_peak = mock_sweep_axis_gain(r, 68.4f);
    MockSweepResponse flat = r;
    flat.peak_hz = NAN;
    const auto floor_only = mock_sweep_axis_gain(flat, 58.4f);
    for (int axis = 0; axis < 3; ++axis) {
        INFO("axis " << axis);
        const float w = r.axis_weight[static_cast<size_t>(axis)];
        CHECK(at_peak[static_cast<size_t>(axis)] ==
              Approx(300.0 * std::sqrt(1.02) * w).epsilon(1e-3));
        CHECK(off_peak[static_cast<size_t>(axis)] ==
              Approx(300.0 * std::sqrt(0.02 + 0.2) * w).epsilon(1e-3));
        CHECK(floor_only[static_cast<size_t>(axis)] ==
              Approx(300.0 * std::sqrt(0.02) * w).epsilon(1e-3));
    }
}

TEST_CASE("MockAccelBatchSource streams a sweep the estimator can follow",
          "[mock][sweep_preview][slow]") {
    const SweepPlan plan{5.0f, 30.0f, 1.0f};
    MockAccelBatchSource source(
        plan, MockSweepResponse{20.0f, 3.0f, 300.0f, {0.671f, 0.671f, 0.316f}}, 50.0);
    std::mutex mutex;
    std::vector<AccelBatch> batches;
    std::vector<float> rates;
    REQUIRE(source.start(
        "", "adxl345",
        [&](const AccelBatch& batch) {
            std::lock_guard<std::mutex> lock(mutex);
            batches.push_back(batch);
            rates.push_back(source.sample_rate_hz());
        },
        [](const std::string&) {}));

    // 1.5 + 25 s of ramp plus the last steps, at 5 stream seconds per batch.
    const double needed_s = MockAccelBatchSource::kDriveLeadS + 25.0 + 2.0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < deadline) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (!batches.empty() && batches.back().samples.back().time >= needed_s) {
                break;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    const auto stop_started = std::chrono::steady_clock::now();
    source.stop();
    CHECK(std::chrono::steady_clock::now() - stop_started < std::chrono::milliseconds(500));
    size_t after_stop = 0;
    {
        std::lock_guard<std::mutex> lock(mutex);
        after_stop = batches.size();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    CHECK(batches.size() == after_stop);

    REQUIRE_FALSE(batches.empty());
    REQUIRE(batches.back().samples.back().time >= needed_s);
    for (float rate : rates) {
        CHECK(rate == 3053.0f);
    }

    // Replay in 300-sample batches, the console leading the ramp by 1.5 s.
    SweepResponseAccumulator acc(plan);
    std::vector<SweepPoint> points;
    std::vector<AccelSample> chunk;
    auto flush = [&]() {
        const double t = chunk.front().time;
        const double drive = plan.freq_start_hz + (t + 1.5 - MockAccelBatchSource::kDriveLeadS);
        if (drive >= plan.freq_start_hz) {
            acc.note_console_frequency(
                std::floor(std::min(static_cast<float>(drive), plan.freq_end_hz)));
        }
        const auto fresh = acc.add_samples(chunk, true, 3053.0f);
        points.insert(points.end(), fresh.begin(), fresh.end());
        chunk.clear();
    };
    for (const auto& batch : batches) {
        CHECK(batch.contiguous());
        for (const auto& s : batch.samples) {
            chunk.push_back(s);
            if (chunk.size() == 300) {
                flush();
            }
        }
    }
    REQUIRE(acc.locked());
    CHECK(std::abs(acc.drive_start_s() - MockAccelBatchSource::kDriveLeadS) < 0.1);
    REQUIRE(points.size() == 26);
    float best_hz = 0.0f;
    float best = -1.0f;
    for (const auto& p : points) {
        if (p.clean && p.psd_xyz() > best) {
            best = p.psd_xyz();
            best_hz = p.freq_hz;
        }
    }
    CHECK(std::abs(best_hz - 20.0f) <= 1.0f);
}
