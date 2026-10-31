// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "belt_tension_types.h"

#include <cstddef>
#include <deque>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

/**
 * @file sweep_response_estimator.h
 * @brief Response of a resonance sweep, one point per Hz, from the raw stream
 *
 * Pure: no LVGL, no threads, no sockets. Callers hand it whatever samples the
 * accelerometer stream delivers and get back the sweep points that finished.
 *
 * The excitation is a linear ramp in time, f(t) = freq_start_hz +
 * hz_per_sec * (t - t0). The console's "Testing frequency" line is printed when
 * a move is planned, a lookahead buffer ahead of the motion, so t0 is found in
 * the samples themselves (DriveLock); the console value only narrows the search.
 */

namespace helix::calibration {

namespace sweep_preview {
/// One point per this many Hz of the ramp.
inline constexpr float kStepHz = 1.0f;
/// Stream seconds between drive-frequency readings.
inline constexpr double kLockIntervalS = 0.25;
/// Samples each reading looks at, ending at the reading's time.
inline constexpr double kLockWindowS = 0.5;
/// The search grid spans [hint - LEAD * hz_per_sec, hint + LAG * hz_per_sec].
inline constexpr double kLockSearchLeadS = 3.0;
inline constexpr double kLockSearchLagS = 0.5;
inline constexpr float kLockGridStepHz = 0.25f;
/// Off-drive reference frequencies, Hz from the grid's argmax. Outside a 0.5 s
/// window's main lobe, so a real drive stands far above them and noise does not.
inline constexpr float kLockReferenceOffsetsHz[] = {-12.0f, -8.0f, 8.0f, 12.0f};
/// The argmax's xyz power over the mean reference power, at least.
inline constexpr float kLockMinContrast = 4.0f;
/// A lock needs this many retained estimates of t0 within kLockAgreeS of one another.
inline constexpr size_t kLockAgreeCount = 5;
inline constexpr double kLockAgreeS = 0.15;
/// Most recent t0 estimates retained while unlocked.
inline constexpr size_t kLockHistory = 16;
/// A locked model is dropped after readings disagree with it by more than
/// kLockLostHz for kLockLostS of stream time without a single agreeing reading.
inline constexpr float kLockLostHz = 1.5f;
inline constexpr double kLockLostS = 3.0;
/// Stream seconds of samples kept for steps that finish before the lock does.
inline constexpr double kBufferS = 8.0;
} // namespace sweep_preview

/// The ramp one sweep drives.
struct SweepPlan {
    float freq_start_hz = 0.0f;
    float freq_end_hz = 0.0f;
    float hz_per_sec = 0.0f;

    /// All finite, freq_start_hz > 0, freq_end_hz > freq_start_hz, hz_per_sec > 0.
    [[nodiscard]] bool valid() const;
    /// floor((end - start) / kStepHz) + 1, or 0 when !valid().
    [[nodiscard]] size_t point_count() const;
    /// freq_start_hz + index * kStepHz.
    [[nodiscard]] float point_hz(size_t index) const;
};

/// One finished step of the sweep.
struct SweepPoint {
    float freq_hz = 0.0f;
    /// Per-axis power at freq_hz, (mm/s^2)^2/Hz like Klipper's CSV columns.
    /// NaN when !clean.
    float psd_x = 0.0f;
    float psd_y = 0.0f;
    float psd_z = 0.0f;
    /// False when the step's samples had a gap, fell out of the buffer, or
    /// finished while the drive was not locked.
    bool clean = false;

    /// psd_x + psd_y + psd_z, as Klipper's psd_xyz column.
    [[nodiscard]] float psd_xyz() const;
};

/// Which CSV column a consumer reads; the preview must read the same one.
enum class SweepColumn {
    XYZ,      ///< belt paths
    X,        ///< input shaper, X axis
    Y,        ///< input shaper, Y axis
    IN_PLANE, ///< psd_x + psd_y: the belt tuning loop's curves
};

/// The point's value in `column`; NaN for an unclean point.
[[nodiscard]] float sweep_column_value(const SweepPoint& point, SweepColumn column);

/**
 * @brief One-sided power spectral density of `x` at one frequency
 *
 * Mean removed, symmetric Hann window w[i] = 0.5 - 0.5 cos(2 pi i / (n - 1)),
 * single-bin DFT accumulated in double, scaled 2 |X|^2 / (rate_hz * sum(w^2))
 * so a sine of amplitude A reads A^2 (sum w)^2 / (2 rate_hz sum(w^2)).
 *
 * @return 0 for n < 2, rate_hz <= 0 or freq_hz <= 0
 */
[[nodiscard]] float goertzel_psd(const float* x, size_t n, float freq_hz, float rate_hz);

/**
 * @brief The drive frequency in one window of samples, if one stands out
 *
 * Grid from hint_hz - kLockSearchLeadS * hz_per_sec to hint_hz +
 * kLockSearchLagS * hz_per_sec at kLockGridStepHz, positive frequencies
 * only; power is goertzel_psd summed over x, y and z. The argmax is refined by
 * a parabola through it and its two neighbours.
 *
 * @return nullopt when the grid has fewer than 3 points, the argmax sits on
 *         either end of the grid, or its power is under kLockMinContrast times
 *         the mean power at kLockReferenceOffsetsHz from it (positive ones only)
 */
[[nodiscard]] std::optional<float> find_drive_hz(const AccelSample* samples, size_t n,
                                                 float rate_hz, float hint_hz, float hz_per_sec);

/**
 * @brief The sweep's start time, from drive-frequency readings
 *
 * Each reading (at_s, drive_hz) estimates t0 = at_s - (drive_hz -
 * freq_start_hz) / hz_per_sec. Locks once kLockAgreeCount of the retained
 * estimates sit within kLockAgreeS of one of them; t0 is the median of that
 * densest cluster and stays fixed while locked. A resonance that captures the
 * argmax for a second or two lands outside the cluster.
 */
class DriveLock {
  public:
    explicit DriveLock(SweepPlan plan);

    /// Feed one reading taken over a window centred on stream time `at_s`.
    void observe(double at_s, float drive_hz);

    [[nodiscard]] bool locked() const;
    /// t0 in stream seconds; NaN before the first lock. Keeps the last lock's
    /// value after the lock is lost.
    [[nodiscard]] double drive_start_s() const;
    /// freq_start_hz + hz_per_sec * (at_s - t0); NaN while unlocked.
    [[nodiscard]] float model_hz(double at_s) const;

  private:
    SweepPlan plan_;
    std::deque<double> estimates_;
    bool locked_ = false;
    double t0_s_ = std::numeric_limits<double>::quiet_NaN();
    /// Stream time the current run of disagreeing readings began; NaN when the
    /// last reading agreed.
    double disagree_since_s_ = std::numeric_limits<double>::quiet_NaN();
};

/**
 * @brief Samples in, finished sweep points out
 *
 * Takes a drive-frequency reading every kLockIntervalS of stream time once a
 * console hint exists. Once DriveLock holds, step k covers drive frequencies
 * [point_hz(k) - kStepHz / 2, point_hz(k) + kStepHz / 2] clipped to the plan,
 * and is emitted when the newest sample passes its end time: goertzel_psd of
 * x, y and z at point_hz(k) over the samples in that span. Every step is
 * emitted exactly once and in order. A step finishing while the lock is lost,
 * or whose span overlaps a non-contiguous batch or starts before the buffer,
 * is emitted unclean.
 */
class SweepResponseAccumulator {
  public:
    explicit SweepResponseAccumulator(SweepPlan plan);

    /// The latest "Testing frequency" value; only narrows the drive search.
    void note_console_frequency(float hz);

    /// Append samples (stream time ascending) and return the points that finished.
    /// `contiguous` false marks the batch's span as a gap. rate_hz <= 0 buffers
    /// the samples but takes no reading.
    [[nodiscard]] std::vector<SweepPoint> add_samples(const std::vector<AccelSample>& samples,
                                                      bool contiguous, float rate_hz);

    [[nodiscard]] bool locked() const;
    [[nodiscard]] double drive_start_s() const;
    /// Every one of plan().point_count() points has been emitted.
    [[nodiscard]] bool complete() const;
    /// Time of the newest sample, 0 before any.
    [[nodiscard]] double stream_time_s() const;
    [[nodiscard]] const SweepPlan& plan() const;

  private:
    SweepPlan plan_;
    DriveLock lock_;
    std::deque<AccelSample> buffer_;
    /// [first, last] stream times of non-contiguous batches still in the buffer.
    std::deque<std::pair<double, double>> gaps_;
    float hint_hz_ = std::numeric_limits<float>::quiet_NaN();
    double next_reading_s_ = 0.0;
    size_t next_point_ = 0;
    double stream_time_s_ = 0.0;
};

} // namespace helix::calibration
