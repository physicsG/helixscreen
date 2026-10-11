// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file belt_tension_types.cpp
 * @brief Implementation of belt tension analysis functions
 *
 * Provides PSD computation via DFT, CSV parsing for Klipper accelerometer
 * data, Pearson correlation for belt path similarity, and two-path curve
 * comparison with a provisional verdict.
 */

#include "belt_tension_types.h"

#include "spdlog/spdlog.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <sstream>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace helix::calibration {

// ============================================================================
// Band-limited comparison: band_similarity() / detect_belt_peaks() /
// pair_belt_peaks() / verdict_for_similarity() / compare_belt_paths()
// ============================================================================

namespace {

/// The curve's bins with band_min_hz <= f <= band_max_hz, ascending frequency.
BeltCurve in_band(const BeltCurve& curve, float band_min_hz, float band_max_hz) {
    BeltCurve out;
    for (const auto& bin : curve) {
        if (bin.first >= band_min_hz && bin.first <= band_max_hz) {
            out.push_back(bin);
        }
    }
    return out;
}

/// Linear interpolation at freq, clamped to the end bins outside the curve
/// (numpy's np.interp semantics).
float interp_at(const BeltCurve& curve, float freq) {
    if (freq <= curve.front().first) {
        return curve.front().second;
    }
    for (size_t i = 1; i < curve.size(); ++i) {
        if (curve[i].first >= freq) {
            const float f0 = curve[i - 1].first;
            const float f1 = curve[i].first;
            if (f1 - f0 < 1e-6f) {
                return curve[i - 1].second;
            }
            const float t = (freq - f0) / (f1 - f0);
            return curve[i - 1].second + t * (curve[i].second - curve[i - 1].second);
        }
    }
    return curve.back().second;
}

/// Percentile of ascending `values` with linear interpolation between closest
/// ranks - numpy's default - so thresholds match the reference computations.
float percentile_linear(const std::vector<float>& values, float pct) {
    const float pos = pct / 100.0f * static_cast<float>(values.size() - 1);
    const size_t lo = static_cast<size_t>(pos);
    const size_t hi = std::min(lo + 1, values.size() - 1);
    return values[lo] + (pos - static_cast<float>(lo)) * (values[hi] - values[lo]);
}

} // namespace

BeltVerdict verdict_for_similarity(float similarity_percent) {
    using namespace belt_verdict;
    return similarity_percent >= MATCHED_SIMILARITY ? BeltVerdict::MATCHED
           : similarity_percent >= CLOSE_SIMILARITY ? BeltVerdict::CLOSE
                                                    : BeltVerdict::ADJUST;
}

float band_similarity(const BeltCurve& a, const BeltCurve& b, float band_min_hz,
                      float band_max_hz) {
    const BeltCurve xa = in_band(a, band_min_hz, band_max_hz);
    const BeltCurve xb = in_band(b, band_min_hz, band_max_hz);
    if (xa.size() < 3 || xb.size() < 3) {
        return 0.0f;
    }

    // Pearson over A's in-band bins with B interpolated onto them. Double
    // accumulators: psd values reach 1e5 and products 1e10, past where float
    // rounding stays quiet in the headline percent.
    std::vector<double> va(xa.size()), vb(xa.size());
    for (size_t i = 0; i < xa.size(); ++i) {
        va[i] = xa[i].second;
        vb[i] = interp_at(xb, xa[i].first);
    }
    const double mean_a =
        std::accumulate(va.begin(), va.end(), 0.0) / static_cast<double>(va.size());
    const double mean_b =
        std::accumulate(vb.begin(), vb.end(), 0.0) / static_cast<double>(vb.size());
    double sum_ab = 0.0, sum_aa = 0.0, sum_bb = 0.0;
    for (size_t i = 0; i < va.size(); ++i) {
        const double da = va[i] - mean_a;
        const double db = vb[i] - mean_b;
        sum_ab += da * db;
        sum_aa += da * da;
        sum_bb += db * db;
    }
    const double denom = std::sqrt(sum_aa * sum_bb);
    if (denom < 1e-12) {
        return 0.0f;
    }
    const double r = sum_ab / denom;
    return static_cast<float>(std::clamp(r * 100.0, 0.0, 100.0));
}

std::vector<BeltPeak> detect_belt_peaks(const BeltCurve& curve, float band_min_hz,
                                        float band_max_hz) {
    const BeltCurve x = in_band(curve, band_min_hz, band_max_hz);
    std::vector<BeltPeak> peaks;
    if (x.size() < 3) {
        return peaks;
    }
    float max_v = 0.0f;
    for (const auto& bin : x) {
        max_v = std::max(max_v, bin.second);
    }
    const float min_amplitude = belt_verdict::PEAK_THRESHOLD_FRACTION * max_v;
    // A bin needs both neighbours in-band, so the band's edge bins cannot be
    // peaks. Ascending frequency comes free from the input order.
    for (size_t i = 1; i + 1 < x.size(); ++i) {
        if (x[i].second > x[i - 1].second && x[i].second >= x[i + 1].second &&
            x[i].second >= min_amplitude) {
            peaks.push_back({x[i].first, x[i].second});
        }
    }
    return peaks;
}

BeltPeakPairing pair_belt_peaks(const std::vector<BeltPeak>& a, const std::vector<BeltPeak>& b) {
    BeltPeakPairing out;

    std::vector<float> dists;
    dists.reserve(a.size() * b.size());
    for (const auto& pa : a) {
        for (const auto& pb : b) {
            dists.push_back(std::abs(pa.freq_hz - pb.freq_hz));
        }
    }
    if (dists.empty()) {
        out.threshold_hz = belt_verdict::PAIR_MAX_HZ;
    } else {
        std::sort(dists.begin(), dists.end());
        const float median = percentile_linear(dists, 50.0f);
        const float iqr = percentile_linear(dists, 75.0f) - percentile_linear(dists, 25.0f);
        out.threshold_hz = std::min(median + 1.5f * iqr, belt_verdict::PAIR_MAX_HZ);
    }

    // Greedy: repeatedly take the globally closest remaining (a, b) inside the
    // threshold. Scanning a then b in order and keeping only strictly closer
    // candidates breaks distance ties toward the lower a, then lower b, index.
    std::vector<bool> used_a(a.size(), false), used_b(b.size(), false);
    for (;;) {
        size_t best_i = a.size(), best_j = 0;
        float best_d = 0.0f;
        for (size_t i = 0; i < a.size(); ++i) {
            if (used_a[i]) {
                continue;
            }
            for (size_t j = 0; j < b.size(); ++j) {
                if (used_b[j]) {
                    continue;
                }
                const float d = std::abs(a[i].freq_hz - b[j].freq_hz);
                if (d <= out.threshold_hz && (best_i == a.size() || d < best_d)) {
                    best_i = i;
                    best_j = j;
                    best_d = d;
                }
            }
        }
        if (best_i == a.size()) {
            break;
        }
        out.pairs.push_back({a[best_i], b[best_j]});
        used_a[best_i] = true;
        used_b[best_j] = true;
    }

    for (size_t i = 0; i < a.size(); ++i) {
        if (!used_a[i]) {
            out.unpaired_a.push_back(a[i]);
        }
    }
    for (size_t j = 0; j < b.size(); ++j) {
        if (!used_b[j]) {
            out.unpaired_b.push_back(b[j]);
        }
    }
    std::sort(out.pairs.begin(), out.pairs.end(), [](const BeltPeakPair& l, const BeltPeakPair& r) {
        return l.a.amplitude + l.b.amplitude > r.a.amplitude + r.b.amplitude;
    });
    return out;
}

BeltComparison compare_belt_paths(const BeltCurve& a, const BeltCurve& b, float band_min_hz,
                                  float band_max_hz) {
    BeltComparison out;
    if (in_band(a, band_min_hz, band_max_hz).size() < 3 ||
        in_band(b, band_min_hz, band_max_hz).size() < 3) {
        return out;
    }
    out.valid = true;
    out.similarity_percent = band_similarity(a, b, band_min_hz, band_max_hz);
    out.verdict = verdict_for_similarity(out.similarity_percent);
    out.peaks = pair_belt_peaks(detect_belt_peaks(a, band_min_hz, band_max_hz),
                                detect_belt_peaks(b, band_min_hz, band_max_hz));
    spdlog::debug("[BeltTension] Comparison: similarity {:.1f}% in [{:.0f},{:.0f}] Hz, {} pairs, "
                  "{}+{} unpaired (threshold {:.2f} Hz)",
                  out.similarity_percent, band_min_hz, band_max_hz, out.peaks.pairs.size(),
                  out.peaks.unpaired_a.size(), out.peaks.unpaired_b.size(), out.peaks.threshold_hz);
    return out;
}

// ============================================================================
// calculate_similarity()
// ============================================================================

float calculate_similarity(const std::vector<std::pair<float, float>>& curve_a,
                           const std::vector<std::pair<float, float>>& curve_b) {
    if (curve_a.empty() || curve_b.empty()) {
        return 0.0f;
    }

    // Determine common frequency range
    float min_freq = std::max(curve_a.front().first, curve_b.front().first);
    float max_freq = std::min(curve_a.back().first, curve_b.back().first);

    if (min_freq >= max_freq) {
        return 0.0f;
    }

    // Interpolate both curves to common frequency bins (1 Hz resolution)
    int num_bins = static_cast<int>(max_freq - min_freq);
    if (num_bins < 2) {
        return 0.0f;
    }

    // Linear interpolation helper
    auto interpolate = [](const std::vector<std::pair<float, float>>& curve, float freq) -> float {
        // Find bracketing points
        for (size_t i = 1; i < curve.size(); ++i) {
            if (curve[i].first >= freq) {
                float f0 = curve[i - 1].first;
                float f1 = curve[i].first;
                float v0 = curve[i - 1].second;
                float v1 = curve[i].second;
                if (std::abs(f1 - f0) < 1e-6f) {
                    return v0;
                }
                float t = (freq - f0) / (f1 - f0);
                return v0 + t * (v1 - v0);
            }
        }
        return curve.back().second;
    };

    // Build interpolated arrays
    std::vector<float> vals_a(num_bins);
    std::vector<float> vals_b(num_bins);

    for (int i = 0; i < num_bins; ++i) {
        float freq = min_freq + static_cast<float>(i);
        vals_a[i] = interpolate(curve_a, freq);
        vals_b[i] = interpolate(curve_b, freq);
    }

    // Pearson correlation coefficient
    float mean_a =
        std::accumulate(vals_a.begin(), vals_a.end(), 0.0f) / static_cast<float>(num_bins);
    float mean_b =
        std::accumulate(vals_b.begin(), vals_b.end(), 0.0f) / static_cast<float>(num_bins);

    float sum_ab = 0.0f;
    float sum_a2 = 0.0f;
    float sum_b2 = 0.0f;

    for (int i = 0; i < num_bins; ++i) {
        float da = vals_a[i] - mean_a;
        float db = vals_b[i] - mean_b;
        sum_ab += da * db;
        sum_a2 += da * da;
        sum_b2 += db * db;
    }

    float denom = std::sqrt(sum_a2 * sum_b2);
    if (denom < 1e-10f) {
        return 0.0f;
    }

    float r = sum_ab / denom;
    // Clamp to [0, 1] and convert to percentage
    return std::clamp(r * 100.0f, 0.0f, 100.0f);
}

// ============================================================================
// parse_accel_csv()
// ============================================================================

std::vector<AccelSample> parse_accel_csv(const std::string& csv_data) {
    std::vector<AccelSample> samples;
    std::istringstream stream(csv_data);
    std::string line;

    while (std::getline(stream, line)) {
        // Skip empty lines and comment headers
        if (line.empty() || line[0] == '#') {
            continue;
        }

        // Parse "time,accel_x,accel_y,accel_z"
        AccelSample sample{};
        char comma1, comma2, comma3;
        std::istringstream line_stream(line);

        if (line_stream >> sample.time >> comma1 >> sample.x >> comma2 >> sample.y >> comma3 >>
            sample.z) {
            if (comma1 == ',' && comma2 == ',' && comma3 == ',') {
                samples.push_back(sample);
            }
        }
    }

    spdlog::debug("[BeltTension] Parsed {} accelerometer samples from CSV", samples.size());
    return samples;
}

// ============================================================================
// compute_psd()
// ============================================================================

std::vector<std::pair<float, float>> compute_psd(const std::vector<AccelSample>& samples,
                                                 float sample_rate, float max_freq_hz) {
    std::vector<std::pair<float, float>> psd;

    if (samples.size() < 4) {
        spdlog::warn("[BeltTension] Too few samples ({}) for PSD computation", samples.size());
        return psd;
    }

    size_t n = samples.size();

    // Compute PSD per-axis and sum (avoids sqrt nonlinearity that creates harmonics
    // when one axis has a large DC component like gravity)
    // This matches Klipper/Shake&Tune's approach.

    // DFT parameters
    if (max_freq_hz <= 0.0f) {
        spdlog::warn("[BeltTension] No bandwidth requested, falling back to 250 Hz - this misses "
                     "the harmonic series pitch estimation needs above ~60 Hz fundamentals");
    }
    const float bandwidth = (max_freq_hz > 0.0f) ? max_freq_hz : 250.0f;
    // Clamp to Nyquist BEFORE the float->size_t cast below: an unclamped bandwidth
    // (e.g. a caller-supplied 1e12 Hz) produces a float product wildly out of
    // size_t range, and casting that is undefined behaviour rather than a
    // large-then-clamped value.
    const float effective_bandwidth = std::min(bandwidth, sample_rate * 0.5f);
    size_t max_bin = std::min(
        n / 2, static_cast<size_t>(effective_bandwidth * static_cast<float>(n) / sample_rate));
    if (max_bin == 0) {
        spdlog::warn("[BeltTension] Bandwidth {:.1f} Hz yields no bins at {:.1f} Hz sample rate",
                     bandwidth, sample_rate);
        return psd;
    }
    float freq_resolution = sample_rate / static_cast<float>(n);

    spdlog::debug("[BeltTension] Computing PSD: {} samples, {:.1f} Hz sample rate, {:.2f} Hz "
                  "resolution, {} bins",
                  n, sample_rate, freq_resolution, max_bin);

    psd.resize(max_bin, {0.0f, 0.0f});

    // Process each axis independently: extract signal, remove DC, apply window, DFT
    auto process_axis = [&](auto accessor) {
        std::vector<float> signal(n);
        for (size_t i = 0; i < n; ++i) {
            signal[i] = accessor(samples[i]);
        }

        // Remove DC offset
        float mean = std::accumulate(signal.begin(), signal.end(), 0.0f) / static_cast<float>(n);
        for (auto& s : signal) {
            s -= mean;
        }

        // Apply Hanning window
        for (size_t i = 0; i < n; ++i) {
            float window =
                0.5f * (1.0f - std::cos(2.0f * static_cast<float>(M_PI) * static_cast<float>(i) /
                                        static_cast<float>(n - 1)));
            signal[i] *= window;
        }

        // DFT for this axis, accumulate power into psd.
        //
        // exp(-j*omega*i) is advanced by complex multiplication rather than
        // recomputed per sample. Measured 5.8x faster on an Allwinner H616.
        // The accumulators are double deliberately: a float phasor accumulates
        // enough rotation error over thousands of samples to smear the peak.
        for (size_t k = 1; k <= max_bin; ++k) {
            const double omega = 2.0 * M_PI * static_cast<double>(k) / static_cast<double>(n);
            const double step_cos = std::cos(omega);
            const double step_sin = std::sin(omega);

            double phasor_cos = 1.0; // angle 0
            double phasor_sin = 0.0;
            double real = 0.0;
            double imag = 0.0;

            for (size_t i = 0; i < n; ++i) {
                const double s = signal[i];
                real += s * phasor_cos;
                imag -= s * phasor_sin;
                const double next_cos = phasor_cos * step_cos - phasor_sin * step_sin;
                phasor_sin = phasor_cos * step_sin + phasor_sin * step_cos;
                phasor_cos = next_cos;
            }

            const double power =
                (real * real + imag * imag) / (static_cast<double>(n) * sample_rate);
            psd[k - 1].second += static_cast<float>(power);
        }
    };

    // Process X, Y, Z axes
    process_axis([](const AccelSample& s) { return s.x; });
    process_axis([](const AccelSample& s) { return s.y; });
    process_axis([](const AccelSample& s) { return s.z; });

    // Fill in frequency values
    for (size_t k = 0; k < max_bin; ++k) {
        psd[k].first = static_cast<float>(k + 1) * freq_resolution;
    }

    spdlog::debug("[BeltTension] PSD computation complete: {} frequency bins", psd.size());
    return psd;
}

// ============================================================================
// find_peak_frequency()
// ============================================================================

PeakResult find_peak_frequency(const std::vector<std::pair<float, float>>& psd, float min_freq,
                               float max_freq) {
    PeakResult result;

    if (psd.empty()) {
        return result;
    }

    for (const auto& [freq, power] : psd) {
        if (freq < min_freq || freq > max_freq) {
            continue;
        }
        if (power > result.amplitude) {
            result.frequency = freq;
            result.amplitude = power;
            result.found = true;
        }
    }

    if (result.found) {
        spdlog::debug("[BeltTension] Peak found at {:.1f} Hz (amplitude: {:.4f})", result.frequency,
                      result.amplitude);
    } else {
        spdlog::warn("[BeltTension] No peak found in range [{:.0f}, {:.0f}] Hz", min_freq,
                     max_freq);
    }

    return result;
}

} // namespace helix::calibration
