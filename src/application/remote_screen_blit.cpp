// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "remote_screen_blit.h"

#include <cstring>

namespace helix {

int remote_frame_src_bpp(RemoteScreenPixelFormat fmt) {
    switch (fmt) {
    case RemoteScreenPixelFormat::BGRA8888:
        return 4;
    case RemoteScreenPixelFormat::RGB565:
        return 2;
    case RemoteScreenPixelFormat::Unknown:
        break;
    }
    return 0;
}

BlitResult blit_remote_frame(const RemoteScreenFrame& f, const BlitTarget& dst) {
    BlitResult res;
    if (f.px_map == nullptr || dst.data == nullptr) {
        res.status = BlitStatus::NoSource;
        return res;
    }
    const int src_bpp = remote_frame_src_bpp(f.src_format);
    if (src_bpp == 0 || f.src_stride == 0 || (dst.bpp != 16 && dst.bpp != 32)) {
        res.status = BlitStatus::UnknownFormat;
        return res;
    }
    const int dst_bpp = dst.bpp / 8;

    // Clamp the rect (display coordinates) into the target.
    int32_t x1 = f.x1 < 0 ? 0 : f.x1;
    int32_t y1 = f.y1 < 0 ? 0 : f.y1;
    int32_t x2 = f.x2 > dst.w - 1 ? dst.w - 1 : f.x2;
    int32_t y2 = f.y2 > dst.h - 1 ? dst.h - 1 : f.y2;
    const int32_t w = x2 - x1 + 1;
    const int32_t h = y2 - y1 + 1;
    if (w <= 0 || h <= 0) {
        res.status = BlitStatus::Empty;
        return res;
    }

    // The same rect relative to px_map's own origin.
    const int32_t src_x1 = x1 - f.px_map_x;
    const int32_t src_y1 = y1 - f.px_map_y;
    const int32_t src_x2 = x2 - f.px_map_x;
    const int32_t src_y2 = y2 - f.px_map_y;
    if (src_x1 < 0 || src_y1 < 0) {
        res.status = BlitStatus::BeforeOrigin;
        return res;
    }

    // A frame past the readable source means our view of area/stride disagrees with
    // the renderer; skipping beats a segfault on the render thread.
    const size_t src_stride = f.src_stride;
    const size_t src_row_bytes = static_cast<size_t>(w) * static_cast<size_t>(src_bpp);
    const int32_t bound_h = f.disp_h > 0 ? f.disp_h : dst.h;
    const size_t src_bound =
        f.px_map_len > 0 ? f.px_map_len : src_stride * static_cast<size_t>(bound_h);
    const size_t last_src = static_cast<size_t>(src_y2) * src_stride +
                            static_cast<size_t>(src_x2 + 1) * static_cast<size_t>(src_bpp);
    if (src_row_bytes > src_stride || last_src > src_bound) {
        res.status = BlitStatus::SourceOutOfBounds;
        return res;
    }

    const size_t dst_row_bytes = static_cast<size_t>(w) * static_cast<size_t>(dst_bpp);
    const size_t last_dst = static_cast<size_t>(y2) * dst.stride +
                            static_cast<size_t>(x2 + 1) * static_cast<size_t>(dst_bpp);
    if (dst_row_bytes > dst.stride || last_dst > dst.len) {
        res.status = BlitStatus::TargetOutOfBounds;
        return res;
    }

    for (int32_t row = 0; row < h; ++row) {
        uint8_t* d = dst.data + static_cast<size_t>(y1 + row) * dst.stride +
                     static_cast<size_t>(x1) * static_cast<size_t>(dst_bpp);
        const uint8_t* s = f.px_map + static_cast<size_t>(src_y1 + row) * src_stride +
                           static_cast<size_t>(src_x1) * static_cast<size_t>(src_bpp);

        if (dst_bpp == src_bpp) {
            std::memcpy(d, s, dst_row_bytes);
        } else if (dst_bpp == 4) {
            for (int32_t col = 0; col < w; ++col) {
                uint8_t r, g, b;
                rgb565_to_rgb(static_cast<uint16_t>(s[0] | (s[1] << 8)), r, g, b);
                s += 2;
                d[0] = b;
                d[1] = g;
                d[2] = r;
                d[3] = 0xFF;
                d += 4;
            }
        } else {
            // BGRA source: B=s[0], G=s[1], R=s[2].
            for (int32_t col = 0; col < w; ++col) {
                const uint16_t p =
                    static_cast<uint16_t>(((s[2] >> 3) << 11) | ((s[1] >> 2) << 5) | (s[0] >> 3));
                s += 4;
                d[0] = static_cast<uint8_t>(p & 0xFF);
                d[1] = static_cast<uint8_t>((p >> 8) & 0xFF);
                d += 2;
            }
        }
    }

    res.status = BlitStatus::Copied;
    res.x = x1;
    res.y = y1;
    res.w = w;
    res.h = h;
    return res;
}

} // namespace helix
