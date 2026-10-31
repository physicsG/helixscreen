// Copyright (C) 2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_ams_preheat_status.cpp
 * @brief While the AMS sidebar preheats for a load, the live temperature
 *        appears once: in the step list's heat step, not also in the status.
 */

#include "ui_ams_sidebar.h"

#include <string>

#include "../catch_amalgamated.hpp"

using helix::ui::preheat_status_text;

TEST_CASE("preheat status: the action when the step list carries the reading",
          "[ams][preheat][preheat_status]") {
    const std::string with_steps = preheat_status_text(true, 29, 255);
    CHECK(with_steps == "Heating nozzle");
    CHECK(with_steps.find("°C") == std::string::npos);
    CHECK(with_steps.find("29") == std::string::npos);

    // With no step list the status is the only place for the reading.
    const std::string alone = preheat_status_text(false, 29, 255);
    CHECK(alone.find("29") != std::string::npos);
    CHECK(alone.find("255") != std::string::npos);
    CHECK(alone.find("°C") != std::string::npos);
}
