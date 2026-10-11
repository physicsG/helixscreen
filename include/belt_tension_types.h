// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>
#include <string>
#include <vector>

/**
 * @file belt_tension_types.h
 * @brief Data structures for belt tension tuning calibration
 *
 * Types for CoreXY/Cartesian belt tension measurement and PSD analysis.
 * Used by BeltTensionCalibrator and the belt tension UI panel/wizard.
 *
 * Z belts are deliberately not supported. Measured on a Voron 2.4: four plucks
 * of one Z belt returned 228, 164, 92 and 58 Hz, versus five identical readings
 * on an A/B belt, with the signal 2-5x the noise floor against 12-14x. The
 * gantry decouples Z belt vibration from a toolhead-mounted accelerometer, and
 * Klipper's TEST_RESONANCES cannot sweep Z at all (resonance_tester.py
 * _parse_axis accepts only x, y, or a two-component XY vector). Supporting Z
 * needs a different sensor location, not different code.
 */

namespace helix::calibration {

// ============================================================================
// Belt Identification
// ============================================================================

/// Belt path identifiers for CoreXY
enum class BeltPath {
    PATH_A, ///< CoreXY diagonal A (1,-1)
    PATH_B, ///< CoreXY diagonal B (1,1)
};

/// Kinematics type detected from printer
enum class KinematicsType {
    UNKNOWN,
    COREXY,
    CARTESIAN,
};

// ============================================================================
// Hardware Detection
// ============================================================================

/// Hardware capabilities detected from printer
struct BeltTensionHardware {
    KinematicsType kinematics = KinematicsType::UNKNOWN;
    bool has_adxl = false;
    std::string kinematics_name; ///< Raw string from Klipper
};

// ============================================================================
// Path Comparison
// ============================================================================

/// One path's response: (frequency Hz, psd_xyz) pairs, ascending frequency.
using BeltCurve = std::vector<std::pair<float, float>>;

enum class BeltVerdict { MATCHED, CLOSE, ADJUST };

/// Provisional: from one real capture pair so far. Replace from more captures
/// before the verdict gains directional advice (prestonbrown/helixscreen#1721).
namespace belt_verdict {
inline constexpr float PEAK_THRESHOLD_FRACTION = 0.10f; ///< of the in-band maximum
inline constexpr float PAIR_MAX_HZ = 10.0f;             ///< pairing distance ceiling
inline constexpr float MATCHED_SIMILARITY = 90.0f;
inline constexpr float CLOSE_SIMILARITY = 75.0f;
} // namespace belt_verdict

/// One local maximum of a curve inside the analysis band.
struct BeltPeak {
    float freq_hz = 0.0f;
    float amplitude = 0.0f;
};

/// A peak from each curve the pairing judged the same resonance.
struct BeltPeakPair {
    BeltPeak a;
    BeltPeak b;
};

/// Peaks of two curves paired by frequency, Shake&Tune's _pair_peaks.
struct BeltPeakPairing {
    std::vector<BeltPeakPair> pairs;  ///< sorted by a.amplitude + b.amplitude, descending
    std::vector<BeltPeak> unpaired_a; ///< A peaks with no B partner within threshold_hz
    std::vector<BeltPeak> unpaired_b;
    float threshold_hz = 0.0f;
};

struct BeltComparison {
    bool valid = false; ///< false when either curve has < 3 in-band bins
    float similarity_percent = 0.0f;
    BeltVerdict verdict = BeltVerdict::ADJUST; ///< from similarity only
    BeltPeakPairing peaks;
};

/// Pearson correlation x100 of the two curves inside the band, clamped to 0..100.
/// B is linearly interpolated onto A's in-band frequencies. Returns 0 when either
/// curve has fewer than 3 in-band bins or zero variance.
[[nodiscard]] float band_similarity(const BeltCurve& a, const BeltCurve& b, float band_min_hz,
                                    float band_max_hz);

/// Local maxima inside the band: value > left neighbour, value >= right neighbour,
/// value >= PEAK_THRESHOLD_FRACTION * the in-band maximum. Ascending frequency.
[[nodiscard]] std::vector<BeltPeak> detect_belt_peaks(const BeltCurve& curve, float band_min_hz,
                                                      float band_max_hz);

/// Greedy closest-first pairing of two peak lists. The distance threshold is
/// min(median + 1.5 * IQR over all |fa - fb|, PAIR_MAX_HZ) - PAIR_MAX_HZ when
/// there are no distances at all.
[[nodiscard]] BeltPeakPairing pair_belt_peaks(const std::vector<BeltPeak>& a,
                                              const std::vector<BeltPeak>& b);

/// Tier from similarity_percent alone.
[[nodiscard]] BeltVerdict verdict_for_similarity(float similarity_percent);

/// Compare two belt-path resonance curves inside the printer's sweep range:
/// band-limited similarity (the headline) and paired peaks. Invalid when either
/// curve has < 3 in-band bins.
[[nodiscard]] BeltComparison compare_belt_paths(const BeltCurve& a, const BeltCurve& b,
                                                float band_min_hz, float band_max_hz);

// ============================================================================
// Analysis Functions
// ============================================================================

/// Calculate Pearson correlation between two PSD curves (returns 0-100)
float calculate_similarity(const std::vector<std::pair<float, float>>& curve_a,
                           const std::vector<std::pair<float, float>>& curve_b);

// ============================================================================
// Accelerometer Data Processing
// ============================================================================

/// Single accelerometer sample from Klipper CSV
struct AccelSample {
    float time;
    float x, y, z;
};

/// Parse Klipper raw CSV accelerometer data
/// Format: #time,accel_x,accel_y,accel_z
std::vector<AccelSample> parse_accel_csv(const std::string& csv_data);

/// Compute PSD via DFT from accelerometer samples.
///
/// Returns vector of (frequency_hz, power) pairs. Bin i sits at
/// (i+1)*resolution - there is no DC bin.
///
/// @param max_freq_hz Highest frequency to compute, clamped to Nyquist. The
///        default preserves the original 250 Hz behaviour. Harmonic analysis
///        needs roughly n_harmonics * f0 of bandwidth or the upper harmonics
///        fall outside the array; see pitch_estimator.h.
std::vector<std::pair<float, float>> compute_psd(const std::vector<AccelSample>& samples,
                                                 float sample_rate = 3200.0f,
                                                 float max_freq_hz = 250.0f);

/// Peak frequency search result
struct PeakResult {
    float frequency = 0.0f;
    float amplitude = 0.0f;
    bool found = false;
};

/// Find peak frequency in PSD data within range
PeakResult find_peak_frequency(const std::vector<std::pair<float, float>>& psd,
                               float min_freq = 20.0f, float max_freq = 200.0f);

// ============================================================================
// Callback Types
// ============================================================================

using BeltHardwareDetectCallback = std::function<void(const BeltTensionHardware&)>;
using BeltProgressCallback = std::function<void(int percent)>;
using BeltErrorCallback = std::function<void(const std::string& message)>;

} // namespace helix::calibration
