// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "remote_screen_sink.h"

#include <cstddef>
#include <cstdint>

namespace helix {

/// Where a remote-screen sink copies a dirty rect: an mmapped framebuffer or a heap
/// shadow. `len` bounds every write, so a target with no mapping slack is safe.
struct BlitTarget {
    uint8_t* data = nullptr;
    size_t len = 0;
    uint32_t stride = 0; ///< Bytes per row.
    int w = 0;
    int h = 0;
    int bpp = 0; ///< 16 (RGB565) or 32 (BGRA).
};

enum class BlitStatus {
    Copied,
    NoSource,          ///< null px_map or null target
    UnknownFormat,     ///< source format or target bpp the blit cannot convert
    Empty,             ///< the rect clamps to nothing inside the target
    BeforeOrigin,      ///< the rect starts before px_map's origin
    SourceOutOfBounds, ///< a read would pass the source's readable length
    TargetOutOfBounds, ///< a write would pass `BlitTarget::len`
};

/// The outcome and the clamped rect actually written (valid when `Copied`).
struct BlitResult {
    BlitStatus status = BlitStatus::NoSource;
    int32_t x = 0;
    int32_t y = 0;
    int32_t w = 0;
    int32_t h = 0;
};

/// Bytes per pixel of @p fmt: 4, 2, or 0 when the format is unknown.
int remote_frame_src_bpp(RemoteScreenPixelFormat fmt);

/// Expand one little-endian RGB565 pixel to 8-bit channels, replicating the high
/// bits into the low ones so full-scale stays full-scale.
inline void rgb565_to_rgb(uint16_t p, uint8_t& r, uint8_t& g, uint8_t& b) {
    const uint8_t r5 = static_cast<uint8_t>((p >> 11) & 0x1F);
    const uint8_t g6 = static_cast<uint8_t>((p >> 5) & 0x3F);
    const uint8_t b5 = static_cast<uint8_t>(p & 0x1F);
    r = static_cast<uint8_t>((r5 << 3) | (r5 >> 2));
    g = static_cast<uint8_t>((g6 << 2) | (g6 >> 4));
    b = static_cast<uint8_t>((b5 << 3) | (b5 >> 2));
}

/**
 * @brief Copy one dirty rect of @p f into @p dst, converting pixel format as needed.
 *
 * The rect is clamped to the target, and the source is indexed relative to
 * `px_map_x/px_map_y` (DIRECT/FULL mode leaves those at 0; PARTIAL mode sets them
 * to the rect's top-left). `px_map_len`, when non-zero, is the authoritative
 * readable length; otherwise the bound is inferred as `src_stride * disp_h`.
 * Nothing is written unless every read and write of the rect is in bounds.
 */
BlitResult blit_remote_frame(const RemoteScreenFrame& f, const BlitTarget& dst);

} // namespace helix
