// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "board_fingerprint.h"

#include "lvgl/lvgl.h"
#include "platform_capabilities.h"

#include <cmath>

namespace helix::ui {

std::string board_fingerprint(const BoardFacts& facts) {
    const long bogomips = std::lround(facts.bogomips / 100.0f) * 100;
    return facts.display_backend + "/" + std::to_string(facts.cores) + "c/" +
           std::to_string(bogomips) + "bm/" + std::to_string(facts.width) + "x" +
           std::to_string(facts.height) + "/" + std::to_string(facts.color_depth) + "bpp";
}

BoardFacts current_board_facts(const helix::PlatformCapabilities& caps,
                               const std::string& display_backend) {
    lv_display_t* disp = lv_display_get_default();
    BoardFacts facts;
    facts.display_backend = display_backend.empty() ? "unknown" : display_backend;
    facts.cores = caps.cpu_cores;
    facts.bogomips = caps.bogomips;
    facts.width = disp ? lv_display_get_horizontal_resolution(disp) : 0;
    facts.height = disp ? lv_display_get_vertical_resolution(disp) : 0;
    facts.color_depth = LV_COLOR_DEPTH;
    return facts;
}

} // namespace helix::ui
