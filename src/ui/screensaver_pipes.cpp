// SPDX-License-Identifier: GPL-3.0-or-later

#ifdef HELIX_ENABLE_SCREENSAVER

#include "screensaver_pipes.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <cstring>

using helix::ui::DirtyRect;
using helix::ui::screensaver::random_below;
using helix::ui::screensaver::unit_random;

// A pipe grows one grid step per STEP_MS of frame time, whatever the timer's rate.
static constexpr uint32_t STEP_MS = 100;
// Most steps one callback draws after a late frame; steps past it are dropped, not owed.
static constexpr uint32_t MAX_STEPS_PER_TICK = 3;
static constexpr float PIPE_RADIUS = 0.22f; // Reference uses 0.2
static constexpr float JOINT_RADIUS =
    PIPE_RADIUS * 1.5f;                      // Reference: ballJointRadius = pipeRadius * 1.5
static constexpr float FOV_DEGREES = 45.0f;  // Reference: PerspectiveCamera(45, ...)
static constexpr float CAM_DISTANCE = 28.0f; // Reference: camera at distance 14 from ±10 grid
static constexpr float PI_F = 3.14159265f;

// ---------- Vector math helpers ----------

static float vec3_dot(const float a[3], const float b[3]) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static void vec3_cross(float out[3], const float a[3], const float b[3]) {
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

static void vec3_normalize(float v[3]) {
    float len = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (len > 0.0001f) {
        v[0] /= len;
        v[1] /= len;
        v[2] /= len;
    }
}

// ---------- Color palette ----------

static constexpr int NUM_PIPE_COLORS = 10;

static lv_color_t get_pipe_color(int index) {
    static const uint8_t palette[][3] = {
        {230, 50, 50},  // red
        {50, 200, 50},  // green
        {70, 70, 240},  // blue
        {230, 220, 40}, // yellow
        {40, 210, 210}, // cyan
        {210, 50, 210}, // magenta
        {240, 140, 20}, // orange
        {140, 230, 20}, // lime
        {30, 140, 240}, // sky blue
        {240, 40, 140}, // hot pink
    };
    int i = index % NUM_PIPE_COLORS;
    return lv_color_make(palette[i][0], palette[i][1], palette[i][2]);
}

// ---------- Camera ----------

void PipesScreensaver::setup_camera() {
    // Randomize camera angle each reset (reference: 50% head-on, 50% random rotation)
    float angle = unit_random(rng()) * 2.0f * PI_F;
    float elevation = 0.3f + unit_random(rng()) * 0.4f;

    cam_pos_[0] = CAM_DISTANCE * std::cos(elevation) * std::cos(angle);
    cam_pos_[1] = CAM_DISTANCE * std::sin(elevation);
    cam_pos_[2] = CAM_DISTANCE * std::cos(elevation) * std::sin(angle);

    // Forward = normalize(origin - camera_pos)
    cam_fwd_[0] = -cam_pos_[0];
    cam_fwd_[1] = -cam_pos_[1];
    cam_fwd_[2] = -cam_pos_[2];
    vec3_normalize(cam_fwd_);

    // Right = normalize(cross(forward, world_up))
    float world_up[3] = {0, 1, 0};
    vec3_cross(cam_right_, cam_fwd_, world_up);
    vec3_normalize(cam_right_);

    // Up = cross(right, forward)
    vec3_cross(cam_up_, cam_right_, cam_fwd_);
    vec3_normalize(cam_up_);

    // Focal length from FOV
    focal_ = static_cast<float>(std::min(screen_w(), screen_h())) /
             (2.0f * std::tan(FOV_DEGREES * 0.5f * PI_F / 180.0f));
}

bool PipesScreensaver::project(float wx, float wy, float wz, int& sx, int& sy, float& depth) const {
    float d[3] = {wx - cam_pos_[0], wy - cam_pos_[1], wz - cam_pos_[2]};

    float vz = vec3_dot(d, cam_fwd_);
    if (vz < 0.1f)
        return false; // Behind camera

    float vx = vec3_dot(d, cam_right_);
    float vy = vec3_dot(d, cam_up_);

    sx = screen_w() / 2 + static_cast<int>(vx * focal_ / vz);
    sy = screen_h() / 2 - static_cast<int>(vy * focal_ / vz);
    depth = vz;

    return true;
}

// ---------- Lifecycle ----------

bool PipesScreensaver::on_start() {
    spdlog::info("[Screensaver] Starting 3D pipes");

    setup_camera();
    reset_grid();

    // Start 2 pipes initially (reference starts 1-3)
    color_index_ = 0;
    for (int i = 0; i < 2; i++) {
        start_new_pipe(pipes_[i]);
    }
    steps_.reset();

    spdlog::debug("[Screensaver] Pipes started ({}x{}, perspective camera)", screen_w(),
                  screen_h());
    return true;
}

void PipesScreensaver::on_stop() {
    for (auto& p : pipes_)
        p.alive = false;
}

// ---------- Grid ----------

void PipesScreensaver::reset_grid() {
    std::memset(grid_, 0, sizeof(grid_));
    total_segments_ = 0;
    color_index_ = 0;
}

void PipesScreensaver::start_new_pipe(ActivePipe& pipe) {
    // Find a random empty cell (like reference: randomIntegerVector3WithinBox)
    for (int attempt = 0; attempt < 100; attempt++) {
        GridPos pos;
        pos.x = random_below(rng(), GRID_DIM);
        pos.y = random_below(rng(), GRID_DIM);
        pos.z = random_below(rng(), GRID_DIM);

        if (!grid_[pos.x][pos.y][pos.z]) {
            pipe.pos = pos;
            grid_[pos.x][pos.y][pos.z] = true;
            pipe.dir = static_cast<Direction>(random_below(rng(), 6));
            pipe.color = get_pipe_color(color_index_++);
            pipe.shadow_color = lv_color_mix(pipe.color, lv_color_black(), LV_OPA_50);
            pipe.highlight_color = lv_color_mix(pipe.color, lv_color_white(), LV_OPA_40);
            pipe.segment_count = 0;
            pipe.alive = true;
            pipe.has_prev_dir = false;

            // Draw initial ball joint (reference: makeBallJoint at start position)
            float wx = static_cast<float>(pos.x - GRID_OFFSET);
            float wy = static_cast<float>(pos.y - GRID_OFFSET);
            float wz = static_cast<float>(pos.z - GRID_OFFSET);
            int sx, sy;
            float depth;
            if (project(wx, wy, wz, sx, sy, depth)) {
                lv_layer_t layer;
                canvas().begin_layer(&layer);
                draw_joint(&layer, sx, sy, depth, pipe);
                canvas().finish_layer(&layer);
            }
            return;
        }
    }
    pipe.alive = false;
}

// ---------- Frame ----------

// Pipes draws in layer sessions, which invalidate the areas they mark, so a frame adds nothing
// to the dirty list.
void PipesScreensaver::on_frame(uint32_t dt_ms, std::vector<DirtyRect>& /*dirty*/) {
    const uint32_t steps = steps_.steps_due(dt_ms, STEP_MS, MAX_STEPS_PER_TICK);
    for (uint32_t step = 0; step < steps; step++) {
        // Reset when grid is full. Steps still due belong to the finished scene, and the
        // new scene's first step comes a full step later.
        if (total_segments_ > MAX_SEGMENTS) {
            canvas().fill_black();
            reset_grid();
            setup_camera(); // New random camera angle on reset
            for (auto& p : pipes_)
                p.alive = false;
            for (int i = 0; i < 2; i++) {
                start_new_pipe(pipes_[i]);
            }
            steps_.reset();
            return;
        }
        grow_step();
    }
}

void PipesScreensaver::grow_step() {
    // Single layer session for all pipe growth this step
    lv_layer_t layer;
    canvas().begin_layer(&layer);

    // Grow each active pipe
    int alive_count = 0;
    for (auto& pipe : pipes_) {
        if (!pipe.alive)
            continue;
        alive_count++;

        if (!grow_pipe(pipe, &layer)) {
            pipe.alive = false;
        }
    }

    canvas().finish_layer(&layer);

    // Start new pipes when old ones die (reference: multiple pipes simultaneously)
    if (alive_count < MAX_ACTIVE_PIPES) {
        for (auto& pipe : pipes_) {
            if (!pipe.alive) {
                start_new_pipe(pipe);
                break;
            }
        }
    }
}

// ---------- Growth ----------

bool PipesScreensaver::advance_pipe(ActivePipe& pipe, Direction dir, GridPos np,
                                    lv_layer_t* layer) {
    grid_[np.x][np.y][np.z] = true;

    const float wx1 = static_cast<float>(pipe.pos.x - GRID_OFFSET);
    const float wy1 = static_cast<float>(pipe.pos.y - GRID_OFFSET);
    const float wz1 = static_cast<float>(pipe.pos.z - GRID_OFFSET);
    const float wx2 = static_cast<float>(np.x - GRID_OFFSET);
    const float wy2 = static_cast<float>(np.y - GRID_OFFSET);
    const float wz2 = static_cast<float>(np.z - GRID_OFFSET);

    int sx1, sy1, sx2, sy2;
    float d1, d2;

    if (project(wx1, wy1, wz1, sx1, sy1, d1) && project(wx2, wy2, wz2, sx2, sy2, d2)) {
        // Ball joint at direction change (reference: makeBallJoint)
        if (pipe.has_prev_dir && dir != pipe.dir) {
            draw_joint(layer, sx1, sy1, d1, pipe);
        }

        draw_segment(layer, sx1, sy1, sx2, sy2, (d1 + d2) * 0.5f, pipe);
    }

    pipe.dir = dir;
    pipe.has_prev_dir = true;
    pipe.pos = np;
    pipe.segment_count++;
    total_segments_++;
    return true;
}

bool PipesScreensaver::grow_pipe(ActivePipe& pipe, lv_layer_t* layer) {
    Direction try_dir = pipe.dir;

    // Reference: chance(1/2) && lastDirectionVector ? continue straight : random direction
    if (pipe.has_prev_dir && random_below(rng(), 2) == 0) {
        // Continue straight (50%)
        try_dir = pipe.dir;
    } else {
        // Random direction — pick random axis + sign (reference: chooseFrom("xyz") + [+1,-1])
        try_dir = static_cast<Direction>(random_below(rng(), 6));
    }

    // Try the chosen direction
    GridPos np = next_pos(pipe.pos, try_dir);
    if (in_bounds(np) && !grid_[np.x][np.y][np.z]) {
        return advance_pipe(pipe, try_dir, np, layer);
    }

    // Fallback: try all 6 directions (reference just returns, but we're at lower tick rate)
    for (int d = 0; d < 6; d++) {
        auto candidate = static_cast<Direction>(d);
        GridPos cnp = next_pos(pipe.pos, candidate);
        if (in_bounds(cnp) && !grid_[cnp.x][cnp.y][cnp.z]) {
            return advance_pipe(pipe, candidate, cnp, layer);
        }
    }

    return false;
}

// ---------- Drawing ----------

void PipesScreensaver::draw_segment(lv_layer_t* layer, int sx1, int sy1, int sx2, int sy2,
                                    float depth, const ActivePipe& pipe) {
    // Pipe thickness scales with depth (perspective foreshortening)
    float projected_radius = PIPE_RADIUS * focal_ / depth;
    int thickness = std::max(3, static_cast<int>(projected_radius * 2.0f));

    // 3-layer cylinder shading using thick lines that follow the actual segment direction.
    // Square ends (no rounding) so consecutive straight segments connect seamlessly.
    int inset1 = std::max(1, thickness / 5);
    int inset2 = std::max(2, thickness / 3);

    // lv_draw_line() draws within its width of the end points; the shadow line is the widest.
    canvas().mark_dirty(std::min(sx1, sx2) - thickness, std::min(sy1, sy2) - thickness,
                        std::max(sx1, sx2) + thickness, std::max(sy1, sy2) + thickness);

    lv_draw_line_dsc_t dsc;
    lv_draw_line_dsc_init(&dsc);
    dsc.opa = LV_OPA_COVER;
    dsc.round_start = 0;
    dsc.round_end = 0;
    dsc.p1.x = sx1;
    dsc.p1.y = sy1;
    dsc.p2.x = sx2;
    dsc.p2.y = sy2;

    // Shadow (outer) — full thickness
    dsc.color = pipe.shadow_color;
    dsc.width = thickness;
    lv_draw_line(layer, &dsc);

    // Base color (middle)
    dsc.color = pipe.color;
    dsc.width = std::max(1, thickness - 2 * inset1);
    lv_draw_line(layer, &dsc);

    // Highlight strip (inner)
    dsc.color = pipe.highlight_color;
    dsc.width = std::max(1, thickness - 2 * inset2);
    lv_draw_line(layer, &dsc);
}

void PipesScreensaver::draw_joint(lv_layer_t* layer, int sx, int sy, float depth,
                                  const ActivePipe& pipe) {
    // Ball joint radius scales with depth (reference: SphereGeometry(ballJointRadius, 8, 8))
    float projected_radius = JOINT_RADIUS * focal_ / depth;
    int ball_r = std::max(3, static_cast<int>(projected_radius));
    // The shadow sphere contains the other two.
    canvas().mark_dirty(sx - ball_r, sy - ball_r, sx + ball_r, sy + ball_r);

    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_opa = LV_OPA_COVER;
    dsc.border_width = 0;

    lv_area_t area;

    // Outer shadow sphere
    dsc.bg_color = pipe.shadow_color;
    dsc.radius = ball_r;
    area.x1 = sx - ball_r;
    area.y1 = sy - ball_r;
    area.x2 = sx + ball_r;
    area.y2 = sy + ball_r;
    lv_draw_rect(layer, &dsc, &area);

    // Middle base color
    int mid_r = ball_r * 3 / 4;
    dsc.bg_color = pipe.color;
    dsc.radius = mid_r;
    area.x1 = sx - mid_r;
    area.y1 = sy - mid_r;
    area.x2 = sx + mid_r;
    area.y2 = sy + mid_r;
    lv_draw_rect(layer, &dsc, &area);

    // Highlight spot (offset upper-left for 3D specular effect)
    int hi_r = std::max(2, ball_r / 3);
    int offset = ball_r / 4;
    dsc.bg_color = pipe.highlight_color;
    dsc.radius = hi_r;
    area.x1 = sx - offset - hi_r;
    area.y1 = sy - offset - hi_r;
    area.x2 = sx - offset + hi_r;
    area.y2 = sy - offset + hi_r;
    lv_draw_rect(layer, &dsc, &area);
}

// ---------- Helpers ----------

PipesScreensaver::GridPos PipesScreensaver::next_pos(GridPos pos, Direction dir) const {
    switch (dir) {
    case Direction::POS_X:
        pos.x += 1;
        break;
    case Direction::NEG_X:
        pos.x -= 1;
        break;
    case Direction::POS_Y:
        pos.y += 1;
        break;
    case Direction::NEG_Y:
        pos.y -= 1;
        break;
    case Direction::POS_Z:
        pos.z += 1;
        break;
    case Direction::NEG_Z:
        pos.z -= 1;
        break;
    }
    return pos;
}

bool PipesScreensaver::in_bounds(GridPos pos) const {
    return pos.x >= 0 && pos.x < GRID_DIM && pos.y >= 0 && pos.y < GRID_DIM && pos.z >= 0 &&
           pos.z < GRID_DIM;
}

#endif // HELIX_ENABLE_SCREENSAVER
