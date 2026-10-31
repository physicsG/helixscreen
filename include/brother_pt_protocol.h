// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "label_printer.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace helix::label {

/// PT raster row: 128 pins = 16 bytes
static constexpr int kBrotherPtRasterRowBytes = 16;

/// Parsed status from a Brother PT printer (32-byte response)
struct BrotherPTMedia {
    uint8_t media_type = 0;   ///< 0x01=laminated, 0x03=non-laminated, 0x11=heat-shrink
    uint8_t width_mm = 0;     ///< Raw status byte: 0=none, 4=3.5mm, 6, 9, 12, 18, 24
    uint8_t error_info_1 = 0; ///< Byte 8: 0x01=no media, 0x04=cutter jam, 0x08=weak battery
    uint8_t error_info_2 = 0; ///< Byte 9: 0x01=wrong media, 0x10=cover open, 0x20=overheating
    uint8_t status_type = 0;  ///< Byte 18: 0x00=ready, 0x01=complete, 0x02=error
    bool valid = false;
};

/// Tape geometry for a given width (128-pin models only)
struct BrotherPTTapeInfo {
    int width_mm;         ///< Status byte value (4 means 3.5mm physical)
    int printable_pins;   ///< Number of usable pins for this tape
    int left_margin_pins; ///< Zero-fill pins before printable area
};

/// Get tape geometry for a status-byte width, or nullptr if unsupported.
const BrotherPTTapeInfo* brother_pt_get_tape_info(int width_mm);

/// Create a LabelSize suitable for the renderer from detected tape width.
std::optional<LabelSize> brother_pt_label_size_for_tape(int width_mm);

/// Build a status request command (invalidate + init + ESC i S)
std::vector<uint8_t> brother_pt_build_status_request();

/// Parse a 32-byte status response
BrotherPTMedia brother_pt_parse_status(const uint8_t* data, size_t len);

/// Human-readable error string, or empty if no error
std::string brother_pt_error_string(const BrotherPTMedia& media);

/// TIFF PackBits compression for a single raster row.
std::vector<uint8_t> brother_pt_packbits_compress(const uint8_t* data, size_t len);

/// Build complete PTCBP raster command sequence.
/// The bitmap should be the label content at 180 dpi (not yet rotated/padded).
/// This function handles rotation, flip, centering, and PackBits compression.
std::vector<uint8_t> brother_pt_build_raster(const LabelBitmap& bitmap, int tape_width_mm);

} // namespace helix::label
