// SPDX-License-Identifier: GPL-3.0-or-later

#ifdef HELIX_ENABLE_SCREENSAVER

#include "screensaver_fireworks.h"

#include <spdlog/spdlog.h>

using helix::ui::DirtyRect;
using helix::ui::FrameTarget;

bool helix::ui::FireworksScreensaver::on_start() {
    spdlog::info("[Screensaver] Starting fireworks");
    FrameTarget frame = canvas().frame();
    sim_.init(frame, rng(), level());
    // Filling the canvas black at creation invalidated all of it and no refresh has run since,
    // so the first refresh shows the sky painted here.
    return true;
}

void helix::ui::FireworksScreensaver::on_frame(uint32_t dt_ms, std::vector<DirtyRect>& dirty) {
    FrameTarget frame = canvas().frame();
    sim_.step(dt_ms, frame, rng(), dirty);
    if (sim_.level() != level()) {
        apply_level(sim_.level());
    }
}

void helix::ui::FireworksScreensaver::on_stop() {
    sim_.clear();
}

#endif // HELIX_ENABLE_SCREENSAVER
