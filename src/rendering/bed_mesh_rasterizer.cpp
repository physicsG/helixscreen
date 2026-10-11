// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#if HELIX_HAS_BED_MESH_3D

/**
 * @file bed_mesh_rasterizer.cpp
 * @brief lv_color_t adapters over PixelBuffer's triangle fills
 */

#include "bed_mesh_rasterizer.h"

#include "bed_mesh_buffer.h"

namespace helix {
namespace mesh {

void fill_triangle_solid(PixelBuffer& buf, int x1, int y1, int x2, int y2, int x3, int y3,
                         lv_color_t color, lv_opa_t opacity) {
    buf.fill_triangle_solid(x1, y1, x2, y2, x3, y3, color.red, color.green, color.blue, opacity);
}

void fill_triangle_gradient(PixelBuffer& buf, int x1, int y1, lv_color_t c1, int x2, int y2,
                            lv_color_t c2, int x3, int y3, lv_color_t c3, lv_opa_t opacity) {
    buf.fill_triangle_gradient(x1, y1, c1.red, c1.green, c1.blue, x2, y2, c2.red, c2.green, c2.blue,
                               x3, y3, c3.red, c3.green, c3.blue, opacity);
}

} // namespace mesh
} // namespace helix

#endif // HELIX_HAS_BED_MESH_3D
