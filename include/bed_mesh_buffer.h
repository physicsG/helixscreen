// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

/**
 * @file bed_mesh_buffer.h
 * @brief ARGB8888 pixel buffer with drawing primitives for off-screen rendering
 *
 * Provides a software pixel buffer that the bed mesh renderer can draw into
 * from a background thread. The main thread then blits the result to screen.
 *
 * Byte order is BGRA (matching LVGL's LV_COLOR_FORMAT_ARGB8888):
 *   pixel[0] = B, pixel[1] = G, pixel[2] = R, pixel[3] = A
 */

#include <cstdint>
#include <vector>

namespace helix {
namespace mesh {

/// What a rendered frame shows. The main thread draws overlays and hit-tests touches
/// against the frame on screen, which can lag a mode or mesh change by one render.
struct FrameInfo {
    bool heatmap = false; ///< 2D heatmap cells, not the 3D view
    int rows = 0;         ///< Mesh dimensions the frame was rendered from
    int cols = 0;
};

class PixelBuffer {
  public:
    FrameInfo info; ///< Set by bed_mesh_renderer_render_to_buffer()

    /**
     * Create a pixel buffer with the given dimensions.
     * Buffer is zero-initialized (transparent black).
     *
     * @param width  Width in pixels (0 creates an empty buffer)
     * @param height Height in pixels (0 creates an empty buffer)
     */
    PixelBuffer(int width, int height);

    int width() const {
        return width_;
    }
    int height() const {
        return height_;
    }
    int stride() const {
        return width_ * 4;
    }

    uint8_t* data() {
        return data_.data();
    }
    const uint8_t* data() const {
        return data_.data();
    }

    /**
     * Direct pixel access with bounds checking.
     * @return Pointer to the first byte (B) of the pixel, or nullptr if out of bounds.
     */
    uint8_t* pixel_at(int x, int y);
    const uint8_t* pixel_at(int x, int y) const;

    /**
     * Clear entire buffer to a solid color.
     * Parameters are in RGBA order; internally stored as BGRA.
     */
    void clear(uint8_t r, uint8_t g, uint8_t b, uint8_t a);

    /**
     * Set a single pixel with alpha blending.
     * Out-of-bounds coordinates are safely ignored.
     * Alpha=0 is a no-op; alpha=255 is a direct write (no blending).
     */
    void set_pixel(int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a);

    /**
     * Fill a horizontal line span with alpha blending.
     * The span is clamped to buffer bounds. Out-of-bounds Y is a no-op.
     *
     * @param x     Starting X coordinate (can be negative for clamping)
     * @param width Number of pixels to fill (0 or negative is a no-op)
     * @param y     Y coordinate of the scanline
     */
    void fill_hline(int x, int width, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a);

    /**
     * Draw a line using Bresenham's algorithm with alpha blending.
     * Endpoints outside the buffer are handled safely (per-pixel bounds check).
     *
     * @param thickness Line thickness in pixels (default 1). Expands perpendicular to the line.
     */
    void draw_line(int x0, int y0, int x1, int y1, uint8_t r, uint8_t g, uint8_t b, uint8_t a,
                   int thickness = 1);

    /**
     * Fill a triangle with a solid color using scanline rasterization.
     * Vertices can be in any order. Degenerate triangles (collinear points) are safely skipped.
     * Scanlines are clipped to buffer Y bounds; X clamping is handled by fill_hline().
     */
    void fill_triangle_solid(int x1, int y1, int x2, int y2, int x3, int y3, uint8_t r, uint8_t g,
                             uint8_t b, uint8_t a);

    /**
     * Fill a triangle with per-vertex color gradient using scanline rasterization.
     * Colors are linearly interpolated along edges, then across each scanline using
     * adaptive segment counts based on span width:
     *   - Width < 3px: average color (solid)
     *   - Width < 20px: 2 segments
     *   - Width 20-49px: 3 segments
     *   - Width >= 50px: 4 segments
     *
     * Degenerate triangles are safely skipped. Clipping same as fill_triangle_solid().
     */
    void fill_triangle_gradient(int x1, int y1, uint8_t r1, uint8_t g1, uint8_t b1, int x2, int y2,
                                uint8_t r2, uint8_t g2, uint8_t b2, int x3, int y3, uint8_t r3,
                                uint8_t g3, uint8_t b3, uint8_t a);

  private:
    int width_;
    int height_;
    std::vector<uint8_t> data_;

    /// Blend a single pixel at the given pointer (no bounds check -- caller must verify).
    void blend_pixel(uint8_t* dst, uint8_t r, uint8_t g, uint8_t b, uint8_t a);
};

} // namespace mesh
} // namespace helix
