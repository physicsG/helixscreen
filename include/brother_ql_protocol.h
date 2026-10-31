// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "label_printer.h"

#include <cstdint>
#include <vector>

namespace helix::label {

/// Raster row width for Brother QL-800/810W/820NWB: 90 bytes = 720 pixels
static constexpr int kBrotherQlRasterRowBytes = 90;

/// Build Brother QL raster protocol bytes from a bitmap.
/// Pure function — no I/O. Output can be sent over TCP, RFCOMM, or any transport.
std::vector<uint8_t> brother_ql_build_raster(const LabelBitmap& bitmap, const LabelSize& size);

/// Build a status request command (invalidate + init + status request).
/// Send this, then read 32 bytes back from the printer.
std::vector<uint8_t> brother_ql_build_status_request();

/// Detected media from a Brother QL status response
struct BrotherQLMedia {
    uint8_t media_type = 0; ///< 0x0A=continuous, 0x0B=die-cut
    uint8_t width_mm = 0;   ///< Media width in mm
    uint8_t length_mm = 0;  ///< Media length in mm (0 for continuous)
    bool valid = false;     ///< True if status was parsed successfully
};

/// Parse a 32-byte Brother QL status response to extract loaded media info.
BrotherQLMedia brother_ql_parse_status(const uint8_t* data, size_t len);

/// Find the best matching LabelSize for detected media, or nullptr if no match.
const LabelSize* brother_ql_match_media(const BrotherQLMedia& media,
                                        const std::vector<LabelSize>& sizes);

} // namespace helix::label
