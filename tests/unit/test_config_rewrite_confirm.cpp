// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_config_rewrite_confirm.cpp
 * @brief Unit tests for the shared printer.cfg rewrite confirmation message
 */

#include "../../include/ui_modal.h"

#include <string>

#include "../catch_amalgamated.hpp"

using helix::ui::config_rewrite_message;

TEST_CASE("The caller's sentence comes first", "[config_rewrite]") {
    const std::string msg = config_rewrite_message("Switching to MPC changes heater control.");
    REQUIRE(msg.rfind("Switching to MPC changes heater control.", 0) == 0);
}

TEST_CASE("The shared tail names printer.cfg and the restart", "[config_rewrite]") {
    // Callers must not be able to describe their own change and quietly drop
    // the consequence: both halves of the warning come from here.
    const std::string msg = config_rewrite_message("Something changes.");
    REQUIRE(msg.find("printer.cfg") != std::string::npos);
    REQUIRE(msg.find("restart") != std::string::npos);
}

TEST_CASE("One space joins the two halves", "[config_rewrite]") {
    const std::string msg = config_rewrite_message("A change.");
    REQUIRE(msg.find("A change.  ") == std::string::npos);
    REQUIRE(msg.find("A change. ") != std::string::npos);
}

TEST_CASE("A caller's trailing space does not double up", "[config_rewrite]") {
    const std::string spaced = config_rewrite_message("A change. ");
    const std::string plain = config_rewrite_message("A change.");
    REQUIRE(spaced == plain);
}

TEST_CASE("An empty description yields the tail with no leading space", "[config_rewrite]") {
    const std::string msg = config_rewrite_message("");
    REQUIRE_FALSE(msg.empty());
    REQUIRE(msg.front() != ' ');
    REQUIRE(msg.find("printer.cfg") != std::string::npos);
}
