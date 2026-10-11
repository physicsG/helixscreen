// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

/**
 * @file shaper_csv_parser.h
 * @brief Parser for Klipper input shaper calibration CSV files
 *
 * Parses CSV output from Klipper's calibrate_shaper.py, extracting raw PSD
 * data and per-shaper filtered response curves for frequency response charting.
 *
 * Klipper CSV format:
 *   freq, psd_x, psd_y, psd_z, psd_xyz, shapers:, zv(59.0), mzv(53.8), ...
 *   5.0,  1.234e-03, 2.345e-03, 1.123e-03, 4.702e-03, , 0.001, 0.001, ...
 */

#include "belt_tension_types.h"
#include "calibration_types.h"

#include <string>
#include <vector>

namespace helix {
namespace calibration {

/**
 * @brief Parsed data from a Klipper shaper calibration CSV file
 */
struct ShaperCsvData {
    std::vector<float> frequencies;                 ///< Frequency bins (Hz)
    std::vector<float> raw_psd;                     ///< Raw PSD for the requested axis
    std::vector<ShaperResponseCurve> shaper_curves; ///< Per-shaper filtered responses
};

/**
 * @brief Parse a Klipper calibration CSV file
 *
 * Extracts frequency bins, raw PSD for the specified axis, and all
 * per-shaper filtered response curves from the CSV data.
 *
 * @param csv_path Path to the CSV file
 * @param axis Axis to extract ('X' or 'Y')
 * @return Parsed data, or empty ShaperCsvData on failure
 */
ShaperCsvData parse_shaper_csv(const std::string& csv_path, char axis);

/**
 * @brief Failure modes when reading a TEST_RESONANCES OUTPUT=resonances CSV
 */
enum class ResonanceCsvError {
    NONE,
    MISSING,       ///< File could not be opened
    EMPTY,         ///< Header parsed but no usable data rows
    MULTI_CHIP,    ///< Per-chip columns (no psd_xyz): caller must pick a chip
    NO_PSD_COLUMN, ///< Header lacks freq or psd_xyz
};

/**
 * @brief One belt path's curve from a TEST_RESONANCES OUTPUT=resonances CSV
 */
struct ResonanceCsvData {
    BeltCurve curve; ///< (freq, psd_xyz), empty unless error == NONE
    ResonanceCsvError error = ResonanceCsvError::NONE;
};

/**
 * @brief Read a TEST_RESONANCES OUTPUT=resonances file
 *
 * Columns are found by header name; extra columns (Kalico's accel_per_hz)
 * are ignored. Rows with fewer cells than the header or a non-numeric cell
 * are dropped.
 */
[[nodiscard]] ResonanceCsvData parse_resonance_csv(const std::string& csv_path);

} // namespace calibration
} // namespace helix
