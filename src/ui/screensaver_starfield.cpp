// SPDX-License-Identifier: GPL-3.0-or-later

#ifdef HELIX_ENABLE_SCREENSAVER

#include "screensaver_starfield.h"

#include <spdlog/spdlog.h>

#include <algorithm>

using helix::ui::DirtyRect;
using helix::ui::FrameTarget;
using helix::ui::StarfieldSim;

bool StarfieldScreensaver::on_start() {
    spdlog::info("[Screensaver] Starting starfield");
    sim_.init(static_cast<uint32_t>(screen_w()), static_cast<uint32_t>(screen_h()), rng());
    sim_.set_active_count(static_cast<int>(star_count(level())));
    spdlog::debug("[Screensaver] Starfield started ({}x{}, {} stars)", screen_w(), screen_h(),
                  sim_.active_count());
    return true;
}

void StarfieldScreensaver::on_frame(uint32_t dt_ms, std::vector<DirtyRect>& dirty) {
    FrameTarget target = canvas().frame();
    sim_.step(dt_ms, target, rng(), dirty);
}

void StarfieldScreensaver::on_level_request(size_t level) {
    apply_level(level);
    sim_.set_active_count(static_cast<int>(star_count(this->level())));
}

size_t StarfieldScreensaver::star_count(size_t level) const {
    // A member sees the private count table, so the two stay the same length here.
    static_assert(sizeof(LEVEL_STAR_COUNTS) / sizeof(LEVEL_STAR_COUNTS[0]) ==
                      sizeof(LEVEL_PERIODS_MS) / sizeof(LEVEL_PERIODS_MS[0]),
                  "every level needs both a frame period and a star count");
    constexpr size_t last = sizeof(LEVEL_STAR_COUNTS) / sizeof(LEVEL_STAR_COUNTS[0]) - 1;
    return static_cast<size_t>(LEVEL_STAR_COUNTS[std::min(level, last)]);
}

void StarfieldScreensaver::on_stop() {
    sim_.stars().clear();
}

#endif // HELIX_ENABLE_SCREENSAVER
