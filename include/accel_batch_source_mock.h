// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "live_sweep_preview.h"
#include "sweep_synth.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

/**
 * @file accel_batch_source_mock.h
 * @brief The mock printer's accelerometer stream for a running sweep
 *
 * Plays SweepSynth through the same callbacks klippy's dump endpoint feeds, on
 * a private hv::EventLoopThread, so --test exercises the whole preview path
 * from the loop thread to the chart.
 */

namespace hv {
class EventLoopThread; // NAMESPACE_OK: libhv's own namespace, forward-declared
}

namespace helix::calibration {

/// What the mock's current sweep resonates like. Matches the CSV the mock
/// writes for the same sweep, so the swap to the CSV curve looks like Klipper's.
struct MockSweepResponse {
    float peak_hz = 58.4f;      ///< NaN: no peak, the floor alone
    float half_width_hz = 5.0f; ///< Lorentzian half width at half maximum
    /// Drive amplitude at the peak. A one-second step of amplitude A reads a
    /// PSD near A^2 / 3 on an axis of weight 1, so sqrt(3 * csv_peak_psd) puts
    /// the preview on the CSV's scale.
    float drive_mm_s2 = 300.0f;
    /// Amplitude per axis: the square roots of the CSV's per-axis power shares.
    std::array<float, 3> axis_weight{0.671f, 0.671f, 0.316f};
};

/// Off-peak level, as a fraction of the peak's power.
inline constexpr float kMockFloor = 0.02f;
/// Hiss amplitude as a fraction of drive_mm_s2.
inline constexpr float kMockNoiseFraction = 0.05f;

/// Per-axis drive amplitude in mm/s^2: drive_mm_s2 * sqrt(kMockFloor +
/// 1 / (1 + ((drive_hz - peak_hz) / half_width_hz)^2)) * axis_weight, the
/// Lorentzian term dropped for a NaN peak.
[[nodiscard]] std::array<float, 3> mock_sweep_axis_gain(const MockSweepResponse& response,
                                                        float drive_hz);

class MockAccelBatchSource final : public IAccelBatchSource {
  public:
    /// Wall milliseconds between batches.
    static constexpr uint32_t kBatchIntervalMs = 100;
    /// The ramp starts this long after the stream does, as a real one trails
    /// the console line the preview attaches on.
    static constexpr double kDriveLeadS = 1.5;

    /// @param sim_speed stream seconds per wall second (the mock's --sim-speed)
    MockAccelBatchSource(SweepPlan plan, MockSweepResponse response, double sim_speed);
    ~MockAccelBatchSource() override;
    MockAccelBatchSource(const MockAccelBatchSource&) = delete;
    MockAccelBatchSource& operator=(const MockAccelBatchSource&) = delete;

    bool start(const std::string& socket_path, const std::string& accel_chip,
               BatchCallback on_batch, ErrorCallback on_error) override;
    void stop() override;
    [[nodiscard]] float sample_rate_hz() const override;

  private:
    /// Loop thread: the next batch's worth of SweepSynth samples.
    void emit_batch();

    SweepSynth synth_;
    double sim_speed_;
    BatchCallback on_batch_;
    std::unique_ptr<hv::EventLoopThread> loop_thread_;
    std::atomic<bool> running_{false};
};

} // namespace helix::calibration
