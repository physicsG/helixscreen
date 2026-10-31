// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "screensaver_frame.h"

#include <cstddef>
#include <cstdint>
#include <vector>

/**
 * @file screensaver_heightfield.h
 * @brief A surface seen at a grazing angle, drawn column by column (voxel space)
 *
 * The camera sits just above the surface looking almost level. Depth slices run front to
 * back, each deeper than the last by a constant factor; for each slice and screen column the
 * scene supplies the surface height and colour there, the renderer projects the height to a
 * row, and when that row rises above the column's current top it fills the span between them
 * and raises the top. Cost is columns x slices samples plus the fills, whatever the scene
 * shows. Water (Aqua Surface) and cloud tops (High Fly) both draw through it.
 *
 * No LVGL. Buffers are sized in init(); render() allocates nothing.
 */

namespace helix::ui {

/// How a span's top edge meets what is behind it.
enum class HeightfieldEdge : uint8_t {
    /// Hard spans, then one pass over the final silhouette: the top pixel takes fractional
    /// coverage and the three rows around the silhouette get a [1 2 1] horizontal smoothing.
    FEATHERED_SILHOUETTE,
    /// The top feather_px rows of each new span composite under what is already there,
    /// coverage rising from 0 to 1 down those rows, and the column's top rises only to the
    /// span's top + feather_px, so farther surface shows through the soft edge.
    SOFT_UNDER,
};

/// Fog amount over depth: t = clamp((z - start) / (end - start), 0, 1), then t^power, or
/// (1 - t)^power when `falling`.
struct HeightfieldFog {
    float start = 0.0f;
    float end = 1.0f;
    float power = 1.0f;
    bool falling = false;

    float at(float z) const;
};

/// Camera, projection and slicing. Distances are world units, rows and focals internal pixels.
struct HeightfieldView {
    int32_t w = 0; ///< internal frame, columns
    int32_t h = 0; ///< internal frame, rows
    float horizon_row = 0.0f;
    float focal_x = 1.0f; ///< px per world unit across, at depth 1
    float focal_y = 1.0f; ///< px per world unit up, at depth 1
    float cam_h = 0.0f;   ///< camera height over height 0
    float z_near = 1.0f;
    float z_far = 2.0f;
    float dz_grow = 1.02f; ///< each slice this much thicker than the one before
    uint16_t slices = 1;
    /// 0 for none; otherwise heights scale by smoothstep((z - z_near) / sink_depth), so near
    /// surface passes under the camera instead of being cut into a wall by the near plane.
    float sink_depth = 0.0f;
    HeightfieldEdge edge = HeightfieldEdge::FEATHERED_SILHOUETTE;
    float feather_px = 0.0f; ///< SOFT_UNDER only
    /// Interpolate each span from this slice's colour at its top to the previous slice's at its
    /// bottom, where the previous slice also drew that column.
    bool span_gradient = true;
    /// False leaves the rows below the first slice's top uncovered, for the scene to fill.
    bool first_slice_fills = true;
    /// What the first slice's span shades toward at its bottom when it fills.
    Rgb below_color{};
    HeightfieldFog fog;
};

/// One depth slice, built once per view; render() does no divide.
struct HeightfieldSlice {
    float z = 0.0f;
    float inv_z = 0.0f;
    float dz = 0.0f;     ///< thickness: distance to the next slice
    float sink = 1.0f;   ///< height scale at this depth
    float fog = 0.0f;    ///< HeightfieldFog::at(z)
    float x_left = 0.0f; ///< camera-relative world x of column 0's centre
    float x_step = 0.0f; ///< world x per column
};

/// Slices from z_near to z_far: z_0 = z_near, dz_0 = (z_far - z_near) * (dz_grow - 1) /
/// (dz_grow^slices - 1), dz_i+1 = dz_i * dz_grow, so the last slice ends at z_far.
void build_heightfield_slices(const HeightfieldView& view, std::vector<HeightfieldSlice>& out);

/// What render() asks of a scene, one slice at a time.
class HeightfieldScene {
  public:
    virtual ~HeightfieldScene() = default;
    /// Fills `heights` (before sink) and `colors` for the view's w columns of slice `index`.
    virtual void sample_slice(size_t index, const HeightfieldSlice& slice, float* heights,
                              Rgb* colors) = 0;
};

/// Bilinear sample of a `w` x `h` grid of floats at fractional (u, v) in grid cells,
/// wrapping both axes when `wrap`, clamping to the edge otherwise.
float bilinear_sample(const float* grid, int32_t w, int32_t h, float u, float v, bool wrap);

/**
 * @brief The column renderer
 *
 * render() leaves, per pixel, a premultiplied colour and a coverage (column-major), the slice
 * where coverage first reached half, and per column the silhouette row. resolve() composes
 * that over a background into a row-major frame at the internal size; upscale_frame() writes
 * that frame into the canvas.
 */
class HeightfieldRenderer {
  public:
    /// No slice reached half coverage at this pixel.
    static constexpr uint16_t kNoDepth = 0xFFFF;

    /// Sizes every buffer for `view` and builds its slices.
    void init(const HeightfieldView& view);
    /// Changes the view. Buffers are resized to fit it; shrinking keeps their capacity, so
    /// returning to a larger view allocates nothing.
    void set_view(const HeightfieldView& view);
    const HeightfieldView& view() const {
        return view_;
    }
    const std::vector<HeightfieldSlice>& slices() const {
        return slices_;
    }

    /// Draws `scene` front to back into the column buffers.
    void render(HeightfieldScene& scene);

    /// Premultiplied colour and coverage at internal (x, y).
    Rgb color_at(int32_t x, int32_t y) const;
    uint8_t coverage_at(int32_t x, int32_t y) const;
    /// Slice index where coverage at (x, y) first reached 128, or kNoDepth.
    uint16_t depth_slice_at(int32_t x, int32_t y) const;
    /// depth_slice_at() for every pixel, column-major: (x, y) is at x * h + y.
    const uint16_t* depth_data() const {
        return depth_.data();
    }
    /// Final silhouette of column `x` as a fractional row; h when nothing drew there.
    float top_at(int32_t x) const;
    /// The first slice's projected row and colour in column `x`.
    float front_top_at(int32_t x) const;
    Rgb front_color_at(int32_t x) const;
    /// Samples and filled span pixels in the last render().
    uint32_t samples() const {
        return samples_;
    }
    uint32_t span_pixels() const {
        return span_pixels_;
    }

    /**
     * @brief Composes rows [y_from, y_to) over a background into `frame` (row-major, w x h)
     *
     * out = colour + (255 - coverage) * background / 255. `background` is a row-major w x h
     * image, or nullptr for `flat` everywhere.
     */
    void resolve(const Rgb* background, Rgb flat, int32_t y_from, int32_t y_to,
                 std::vector<Rgb>& frame) const;

    size_t heap_bytes() const;
    void clear();

  private:
    /// FEATHERED_SILHOUETTE's pass over the final silhouette.
    void feather_silhouette();

    HeightfieldView view_;
    std::vector<HeightfieldSlice> slices_;
    std::vector<Rgb> color_;     ///< column-major, w x h
    std::vector<uint8_t> cover_; ///< column-major, w x h
    std::vector<uint16_t> depth_;
    std::vector<float> top_;            ///< per column: the y-buffer, fractional
    std::vector<float> silhouette_;     ///< per column: highest row any slice reached
    std::vector<Rgb> silhouette_color_; ///< per column: the colour of the slice that reached it
    std::vector<float> front_top_;      ///< per column
    std::vector<Rgb> front_color_;
    std::vector<Rgb> last_color_;    ///< per column: the previous slice's colour
    std::vector<uint8_t> last_drew_; ///< per column: the previous slice drew there
    std::vector<float> heights_;     ///< one slice of scene samples
    std::vector<Rgb> colors_;
    std::vector<Rgb> edge_color_; ///< w x 3 rows around the silhouette, smoothed
    std::vector<uint8_t> edge_cover_;
    uint32_t samples_ = 0;
    uint32_t span_pixels_ = 0;
};

/**
 * @brief Writes rows [y_from, y_to) and columns [x_from, x_to) of a row-major `w` x `h` frame
 * into `out`, each pixel filling `scale` x `scale` canvas pixels
 *
 * Scale 2 interpolates bilinearly (weights 3/4 and 1/4 between neighbours, rows clamped to the
 * band written, columns reaching one past the range); any other scale replicates. RGB565
 * output is dithered (PixelWriter::put_dithered). Returns the canvas box written.
 */
DirtyRect upscale_frame(const std::vector<Rgb>& frame, int32_t w, int32_t h, int32_t scale,
                        int32_t y_from, int32_t y_to, FrameTarget& out, int32_t x_from = 0,
                        int32_t x_to = INT32_MAX);

} // namespace helix::ui
