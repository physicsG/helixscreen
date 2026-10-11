// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#ifdef HELIX_ENABLE_SCREENSAVER

#include "screensaver_base.h"
#include "screensaver_motion.h"

#include <cstdint>
#include <lvgl.h>
#include <optional>
#include <vector>

/**
 * @brief Windows-style 3D Pipes screensaver
 *
 * Pipes grow through a 3D grid rendered with perspective projection.
 * Multiple pipes grow simultaneously, each with a different color.
 * Ball joints appear at direction changes. Camera angle randomizes on reset.
 * Ported from https://github.com/1j01/pipes
 */
class PipesScreensaver : public helix::ui::SaverBase {
  public:
    ScreensaverType type() const override {
        return ScreensaverType::PIPES_3D;
    }

  protected:
    std::optional<lv_color_format_t> canvas_format() const override {
        return helix::ui::PIPES_CANVAS_FORMAT;
    }
    bool on_start() override;
    void on_frame(uint32_t dt_ms, std::vector<helix::ui::DirtyRect>& dirty) override;
    void on_stop() override;
    size_t ladder_size() const override {
        return helix::ui::TWO_LEVEL_COUNT;
    }
    uint32_t ladder_period_ms(size_t level) const override {
        return helix::ui::two_level_period_ms(level);
    }

  private:
    // Test-only seam: reads and sets the grid and pipe state. See
    // tests/test_helpers/screensaver_test_access.h.
    friend class PipesScreensaverTestAccess;

    // Grid: 21x21x21 centered at origin (-10..+10), matching reference
    static constexpr int GRID_DIM = 21;
    static constexpr int GRID_OFFSET = 10;
    static constexpr int MAX_SEGMENTS = 500;
    static constexpr int MAX_ACTIVE_PIPES = 3;

    enum class Direction { POS_X = 0, NEG_X, POS_Y, NEG_Y, POS_Z, NEG_Z };

    struct GridPos {
        int x = 0, y = 0, z = 0;
    };

    struct ActivePipe {
        GridPos pos;
        Direction dir{};
        lv_color_t color{};
        lv_color_t shadow_color{};
        lv_color_t highlight_color{};
        int segment_count = 0;
        bool alive = false;
        bool has_prev_dir = false;
    };

    void reset_grid();
    void setup_camera();
    void start_new_pipe(ActivePipe& pipe);
    bool grow_pipe(ActivePipe& pipe, lv_layer_t* layer);
    /// Occupies `np`, draws the segment reaching it, and moves the pipe there. The caller has
    /// already established that `np` is in bounds and free.
    bool advance_pipe(ActivePipe& pipe, Direction dir, GridPos np, lv_layer_t* layer);
    bool project(float wx, float wy, float wz, int& sx, int& sy, float& depth) const;
    void draw_segment(lv_layer_t* layer, int sx1, int sy1, int sx2, int sy2, float depth,
                      const ActivePipe& pipe);
    void draw_joint(lv_layer_t* layer, int sx, int sy, float depth, const ActivePipe& pipe);
    GridPos next_pos(GridPos pos, Direction dir) const;
    bool in_bounds(GridPos pos) const;

    /// Grows every alive pipe one grid step, and starts a pipe when one has died
    void grow_step();

    // Grid occupancy
    bool grid_[GRID_DIM][GRID_DIM][GRID_DIM]{};

    // Multiple active pipes
    ActivePipe pipes_[MAX_ACTIVE_PIPES]{};
    int color_index_ = 0;
    int total_segments_ = 0;

    helix::ui::screensaver::StepAccumulator steps_;

    // Perspective camera (precomputed basis vectors)
    float cam_pos_[3]{};
    float cam_right_[3]{};
    float cam_up_[3]{};
    float cam_fwd_[3]{};
    float focal_ = 0;
};

#endif // HELIX_ENABLE_SCREENSAVER
