// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <vector>

namespace helix {

/// Quality the screen server encodes at: text stays legible, a frame stays under ~120 KB.
constexpr int kRemoteScreenJpegQuality = 70;

/// Convert a w x h block at @p src_bpp 2 (RGB565 little-endian) or 4 (BGRA) into
/// tightly packed RGB888 at @p dst, which must hold w*h*3 bytes.
void convert_to_rgb888(const uint8_t* src, uint32_t src_stride, int w, int h, int src_bpp,
                       uint8_t* dst);

/// Encode packed RGB888 as a baseline JPEG into @p out, replacing its contents.
/// @p quality is clamped to [1, 100]. Returns false, with @p out empty, on a
/// non-positive size, a null @p rgb, or an encoder failure.
bool encode_jpeg_rgb888(const uint8_t* rgb, int w, int h, int quality, std::vector<uint8_t>& out);

} // namespace helix
