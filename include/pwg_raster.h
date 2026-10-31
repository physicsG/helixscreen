// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "label_bitmap.h"

#include <cstdint>
#include <string>
#include <vector>

namespace helix::pwg {

/// PWG Raster color spaces
enum class ColorSpace : uint32_t {
    SGRAY = 18, // sGray (8-bit grayscale)
    SRGB = 19,  // sRGB (24-bit color)
};

/// Parameters for a PWG Raster page header (1796 bytes, all fields big-endian).
/// Only the fields we actually use are listed — rest are zero-filled.
struct PageParams {
    uint32_t hw_resolution_x = 300; // Horizontal DPI
    uint32_t hw_resolution_y = 300; // Vertical DPI
    uint32_t width_px = 0;          // Page width in pixels
    uint32_t height_px = 0;         // Page height in pixels
    ColorSpace color_space = ColorSpace::SGRAY;
    uint32_t bits_per_color = 8;
    uint32_t bits_per_pixel = 8;           // 8 for SGRAY, 24 for SRGB
    uint32_t num_colors = 1;               // 1 for SGRAY, 3 for SRGB
    std::string media_type = "stationery"; // PWG media type keyword
    std::string page_size_name;            // PWG media size name, e.g. "iso_a4_210x297mm"
    uint32_t total_page_count = 1;         // Pages in the document; 0 = unknown
};

/// Generate a complete PWG Raster document from a full-page bitmap.
/// The bitmap should already be at the target page resolution (DPI * page_size).
/// Output is: "RaS2" sync + page header + PackBits-compressed rows.
/// The bitmap is 1bpp but output is 8bpp grayscale (0x00=black, 0xFF=white).
std::vector<uint8_t> generate(const LabelBitmap& page_bitmap, const PageParams& params);

/// Generate PWG Raster for multiple pages
std::vector<uint8_t> generate_multi(const std::vector<const LabelBitmap*>& pages,
                                    const PageParams& params);

/// PackBits-encode a single scanline (exposed for testing)
void packbits_encode(const uint8_t* line, size_t len, std::vector<uint8_t>& out);

/// Build the 1796-byte PWG page header (exposed for testing)
std::vector<uint8_t> build_page_header(const PageParams& params);

} // namespace helix::pwg
