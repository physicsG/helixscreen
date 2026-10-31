// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// What a sliced 3MF ("X.gcode.3mf", as a slicer with 3MF output uploads it)
// holds, read from its ZIP central directory: one Metadata/plate_<N>.gcode per
// plate that was sliced, its .md5, the plate's renders, and slice_info.config
// with each sliced plate's predicted time and filament use. Slicers that write
// model_settings.config there also name each plate and point at its G-code and
// render; those pointers win over the plate_<N> names when the entry they name
// exists. A project 3MF that was never sliced has the renders and no plate G-code.

#include "zip_reader.h"

#include <string>
#include <string_view>
#include <vector>

namespace helix::three_mf {

/// The archive entry carrying every sliced plate's summary.
inline constexpr const char* kSliceInfoEntry = "Metadata/slice_info.config";

/// The archive entry naming each plate and pointing at its G-code and render.
inline constexpr const char* kModelSettingsEntry = "Metadata/model_settings.config";

/// Largest model_settings.config inflated into memory.
inline constexpr size_t kModelSettingsMaxBytes = 256 * 1024;

/// Longest plate name kept, in bytes; a longer one is cut at a character boundary.
inline constexpr size_t kMaxPlateNameBytes = 32;

/// Highest filament slot slice_info may name; a slot past it is not a filament.
inline constexpr int kMaxFilamentSlots = 64;

/// Highest plate number slice_info or an entry name may carry.
inline constexpr int kMaxPlates = 1000;

/// Largest slice_info.config inflated into memory.
inline constexpr size_t kSliceInfoMaxBytes = 256 * 1024;

/// One sliced plate's entries, pointing into the entry list it was found in.
struct ThreeMfPlate {
    int index = 0;    ///< the slicer's plate number, 1-based
    std::string name; ///< the plate's name in the project; empty when it has none
    const zip::ZipEntry* gcode = nullptr;
    const zip::ZipEntry* md5 = nullptr;         ///< hex MD5 of the G-code, when present
    const zip::ZipEntry* thumb = nullptr;       ///< Metadata/plate_<N>.png
    const zip::ZipEntry* thumb_small = nullptr; ///< Metadata/plate_<N>_small.png
};

/// One plate's block of model_settings.config.
struct PlateSettings {
    int plate = 0;              ///< plater_id
    std::string name;           ///< plater_name, entities decoded; may be empty
    std::string gcode_file;     ///< the entry holding the plate's G-code; may be empty
    std::string thumbnail_file; ///< the entry holding the plate's render; may be empty
};

/// Every plate block of model_settings.config with a readable plate number.
std::vector<PlateSettings> parse_model_settings(std::string_view xml);

/**
 * @brief Every sliced plate, by plate number. Empty for a project never sliced.
 *
 * A plate's G-code and render are the entries @p settings points at when those
 * exist, else Metadata/plate_<N>.gcode and .png. An entry one plate points at
 * is that plate's alone, even when its name says another plate.
 */
std::vector<ThreeMfPlate> sliced_plates(const std::vector<zip::ZipEntry>& entries,
                                        const std::vector<PlateSettings>& settings = {});

/// The entry named @p name, or nullptr.
const zip::ZipEntry* find_archive_entry(const std::vector<zip::ZipEntry>& entries,
                                        std::string_view name);

/// One filament a plate uses.
struct SliceFilament {
    int id = 0; ///< 1-based filament slot
    std::string type;
    std::string color; ///< "#RRGGBB" as the slicer wrote it
    double used_m = 0;
    double used_g = 0;
};

/// One plate's slice summary.
struct SliceInfo {
    int plate = 0;
    double prediction_s = 0; ///< predicted print time
    double weight_g = 0;
    std::vector<SliceFilament> filaments;
};

/// Reads plate @p plate's block out of slice_info.config. False when the XML
/// has no block for that plate.
bool parse_slice_info(std::string_view xml, int plate, SliceInfo& out);

/**
 * @brief Name of the G-code extracted from one plate of @p three_mf_path.
 *
 * Keeps the directory, drops ".3mf" and a ".gcode" under it, and adds
 * "_plate<N>" when the archive holds several sliced plates (@p multi), so each
 * plate gets its own name: "a/X.gcode.3mf" gives "a/X.gcode", or "a/X_plate9.gcode".
 */
std::string extracted_gcode_name(std::string_view three_mf_path, int plate, bool multi);

/// The plate a plate G-code entry name carries ("Metadata/plate_9.gcode" -> 9),
/// or 0 when the name is not one.
int plate_of_gcode_entry(std::string_view name);

} // namespace helix::three_mf
