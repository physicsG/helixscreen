// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ui_screensaver.h"

#include "refresh_period_hold_test_access.h"
#include "screensaver_base.h"
#include "screensaver_pipes.h"
#include "screensaver_starfield.h"

#ifdef HELIX_ENABLE_SCREENSAVER

#include "misc/lv_timer_private.h"

#include <cstdint>
#include <cstring>
#include <iterator>
#include <vector>

// Test-only seam (prestonbrown/helixscreen#1591). The screensavers keep their
// draw buffers, timers and simulation state private, and what the tests pin (the
// stride contract, frame-time-driven motion, seeded replay) is only observable
// from that state. Keeping these out of the production headers satisfies the
// "no _for_testing methods in headers" lint (mirrors display_manager_test_access.h).

/// Stops a screensaver however a test exits, so its overlay and timer never outlive the test.
template <typename Saver> struct ScreensaverStopOnExit {
    Saver& saver;
    ~ScreensaverStopOnExit() {
        saver.stop();
    }
};

/// Sets the default display's refresh timer period for the life of the object.
class ScopedRefreshPeriod {
  public:
    explicit ScopedRefreshPeriod(uint32_t period_ms)
        : timer_(lv_display_get_refr_timer(lv_display_get_default())), saved_(timer_->period) {
        lv_timer_set_period(timer_, period_ms);
    }
    ~ScopedRefreshPeriod() {
        lv_timer_set_period(timer_, saved_);
    }

    ScopedRefreshPeriod(const ScopedRefreshPeriod&) = delete;
    ScopedRefreshPeriod& operator=(const ScopedRefreshPeriod&) = delete;

  private:
    lv_timer_t* timer_;
    uint32_t saved_;
};

/// Exact float comparison that states the intent: the same computation must give the same bits.
inline bool screensaver_same_bits(float a, float b) {
    return std::memcmp(&a, &b, sizeof(float)) == 0;
}

namespace helix::test {

/// Records every area invalidated on `disp`, for as long as it lives.
class InvalidatedAreas {
  public:
    explicit InvalidatedAreas(lv_display_t* disp) : disp_(disp) {
        lv_display_add_event_cb(disp_, on_invalidate, LV_EVENT_INVALIDATE_AREA, this);
    }
    ~InvalidatedAreas() {
        lv_display_remove_event_cb_with_user_data(disp_, on_invalidate, this);
    }
    InvalidatedAreas(const InvalidatedAreas&) = delete;
    InvalidatedAreas& operator=(const InvalidatedAreas&) = delete;

    std::vector<lv_area_t> areas;

  private:
    static void on_invalidate(lv_event_t* e) {
        auto* self = static_cast<InvalidatedAreas*>(lv_event_get_user_data(e));
        self->areas.push_back(*static_cast<const lv_area_t*>(lv_event_get_param(e)));
    }

    lv_display_t* disp_;
};

} // namespace helix::test

namespace helix::ui {

/// Reaches the parts every saver keeps in SaverBase.
class SaverTestAccess {
  public:
    static lv_obj_t* overlay(const SaverBase& saver) {
        return saver.overlay_.obj();
    }
    static lv_obj_t* canvas(const SaverBase& saver) {
        return saver.canvas_.obj();
    }
    static lv_timer_t* timer(const SaverBase& saver) {
        return saver.timer_.timer();
    }
    static size_t draw_buf_size(const SaverBase& saver) {
        return saver.canvas_.buffer_size();
    }
    static uint32_t draw_buf_stride(const SaverBase& saver) {
        return saver.canvas_.stride();
    }
    static void set_fixed_seed(SaverBase& saver, uint32_t seed) {
        saver.fixed_seed_ = seed;
    }
};

/// Leaves the global refresh hold released with no configured period, on entry and however
/// the test exits: the configured period is level 0's frame period for every saver started.
struct ScopedGlobalRefreshHold {
    ScopedGlobalRefreshHold() {
        helix::RefreshPeriodHoldTestAccess::reset(helix::active_refresh_period_hold());
    }
    ~ScopedGlobalRefreshHold() {
        helix::RefreshPeriodHoldTestAccess::reset(helix::active_refresh_period_hold());
    }
    ScopedGlobalRefreshHold(const ScopedGlobalRefreshHold&) = delete;
    ScopedGlobalRefreshHold& operator=(const ScopedGlobalRefreshHold&) = delete;
};

/// Level 0's frame timer period and the held display refresh period of one saver.
struct LevelZeroPeriods {
    uint32_t timer_ms = 0;
    uint32_t refresh_ms = 0;
};

/// Starts a fresh `Saver` while the global refresh hold is out, as ScreensaverManager runs
/// one, with `configured_ms` as HELIX_SCREENSAVER_REFR_PERIOD_MS (0 for unset), and reads
/// its level 0 periods.
template <typename Saver> LevelZeroPeriods level_zero_periods(uint32_t configured_ms) {
    helix::ScopedTimerPeriods restore;
    helix::ScopedTimerPeriods::set(40, 40); // unlike 16, 20 and 33
    ScopedGlobalRefreshHold clean_hold;
    helix::RefreshPeriodHold& hold = helix::active_refresh_period_hold();
    hold.set_period(configured_ms);
    hold.acquire();
    LevelZeroPeriods periods;
    Saver saver;
    ScreensaverStopOnExit<Saver> stop_on_exit{saver};
    saver.start();
    if (saver.is_active() && SaverTestAccess::timer(saver) != nullptr) {
        periods.timer_ms = SaverTestAccess::timer(saver)->period;
    }
    periods.refresh_ms = helix::default_refr_timer_period();
    saver.stop();
    hold.release();
    return periods;
}

} // namespace helix::ui

using helix::ui::level_zero_periods;
using helix::ui::LevelZeroPeriods;
using helix::ui::SaverTestAccess;
using helix::ui::ScopedGlobalRefreshHold;

class FlyingToasterScreensaverTestAccess {
  public:
    struct Sprite {
        lv_obj_t* img;
        bool is_toaster;
        int32_t start_x;
        int32_t start_y;
        int32_t fly_ms;
        int32_t delay_ms;
    };

    static std::vector<Sprite> sprites(const FlyingToasterScreensaver& ss) {
        std::vector<Sprite> out;
        for (const auto& obj : ss.m_objects) {
            out.push_back(
                {obj.img, obj.is_toaster, obj.start_x, obj.start_y, obj.fly_ms, obj.delay_ms});
        }
        return out;
    }

    static bool frames_decoded(const FlyingToasterScreensaver& ss) {
        for (const auto* buf : ss.m_decoded_frames) {
            if (!buf) {
                return false;
            }
        }
        return true;
    }

    /// Index of the wing frame `img` shows, or -1 when it shows none of them (toast).
    static int frame_of(const FlyingToasterScreensaver& ss, lv_obj_t* img) {
        const void* src = lv_image_get_src(img);
        for (size_t i = 0; i < std::size(ss.m_decoded_frames); i++) {
            if (src == ss.m_decoded_frames[i]) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }
};

class StarfieldScreensaverTestAccess {
  public:
    struct StarState {
        float x;
        float y;
        float z;
        float speed;
        uint8_t tint_r;
        uint8_t tint_g;
        uint8_t tint_b;

        bool operator==(const StarState& o) const {
            return screensaver_same_bits(x, o.x) && screensaver_same_bits(y, o.y) &&
                   screensaver_same_bits(z, o.z) && screensaver_same_bits(speed, o.speed) &&
                   tint_r == o.tint_r && tint_g == o.tint_g && tint_b == o.tint_b;
        }
    };

    static std::vector<StarState> stars(const StarfieldScreensaver& ss) {
        std::vector<StarState> out;
        for (const auto& s : ss.sim_.stars()) {
            out.push_back({s.x, s.y, s.z, s.speed, s.tint_r, s.tint_g, s.tint_b});
        }
        return out;
    }

    /// Moves every star to one point, keeping each star's own speed.
    static void place_stars(StarfieldScreensaver& ss, float x, float y, float z) {
        for (auto& s : ss.sim_.stars()) {
            s.x = x;
            s.y = y;
            s.z = z;
        }
    }

    /// Stars the sim moves and draws per frame at the saver's current level.
    static int active_star_count(const StarfieldScreensaver& ss) {
        return ss.sim_.active_count();
    }

    /// Rungs on the starfield's ladder.
    static size_t ladder_size(const StarfieldScreensaver& ss) {
        return ss.ladder_size();
    }

    /// Stars the ladder flies at `level`, without starting the saver.
    static size_t star_count(const StarfieldScreensaver& ss, size_t level) {
        return ss.star_count(level);
    }
};

class PipesScreensaverTestAccess {
  public:
    struct PipeState {
        int x;
        int y;
        int z;
        int dir;
        int segment_count;
        bool alive;

        bool operator==(const PipeState& o) const {
            return x == o.x && y == o.y && z == o.z && dir == o.dir &&
                   segment_count == o.segment_count && alive == o.alive;
        }
    };

    static int max_segments() {
        return PipesScreensaver::MAX_SEGMENTS;
    }

    static int total_segments(const PipesScreensaver& ss) {
        return ss.total_segments_;
    }

    static void set_total_segments(PipesScreensaver& ss, int total) {
        ss.total_segments_ = total;
    }

    static std::vector<PipeState> pipes(const PipesScreensaver& ss) {
        std::vector<PipeState> out;
        for (const auto& p : ss.pipes_) {
            out.push_back(
                {p.pos.x, p.pos.y, p.pos.z, static_cast<int>(p.dir), p.segment_count, p.alive});
        }
        return out;
    }

    /// True when both savers hold the same grid occupancy, pipes and camera.
    static bool same_scene(const PipesScreensaver& a, const PipesScreensaver& b) {
        return std::memcmp(a.grid_, b.grid_, sizeof(a.grid_)) == 0 &&
               std::memcmp(a.cam_pos_, b.cam_pos_, sizeof(a.cam_pos_)) == 0 && pipes(a) == pipes(b);
    }
};

#endif // HELIX_ENABLE_SCREENSAVER
