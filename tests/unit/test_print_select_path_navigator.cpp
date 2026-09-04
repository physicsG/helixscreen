// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_print_select_path_navigator.h"

#include "../catch_amalgamated.hpp"

using helix::ui::PrintSelectPathNavigator;

TEST_CASE("[PathNavigator] Default state is at root") {
    PrintSelectPathNavigator nav;

    REQUIRE(nav.current_path().empty());
    REQUIRE(nav.is_at_root());
}

TEST_CASE("[PathNavigator] navigate_to updates path") {
    PrintSelectPathNavigator nav;

    nav.navigate_to("subdir");

    REQUIRE(nav.current_path() == "subdir");
}

TEST_CASE("[PathNavigator] navigate_to concatenates paths") {
    PrintSelectPathNavigator nav;

    nav.navigate_to("subdir");
    nav.navigate_to("another");

    REQUIRE(nav.current_path() == "subdir/another");
}

TEST_CASE("[PathNavigator] navigate_up pops one level") {
    PrintSelectPathNavigator nav;
    nav.navigate_to("a");
    nav.navigate_to("b");
    REQUIRE(nav.current_path() == "a/b");

    nav.navigate_up();

    REQUIRE(nav.current_path() == "a");
}

TEST_CASE("[PathNavigator] navigate_up from single level goes to root") {
    PrintSelectPathNavigator nav;
    nav.navigate_to("subdir");
    REQUIRE(nav.current_path() == "subdir");

    nav.navigate_up();

    REQUIRE(nav.current_path().empty());
    REQUIRE(nav.is_at_root());
}

TEST_CASE("[PathNavigator] navigate_up at root is no-op") {
    PrintSelectPathNavigator nav;
    REQUIRE(nav.is_at_root());

    nav.navigate_up();

    REQUIRE(nav.current_path().empty());
    REQUIRE(nav.is_at_root());
}

TEST_CASE("[PathNavigator] is_at_root after navigation") {
    PrintSelectPathNavigator nav;
    REQUIRE(nav.is_at_root());

    nav.navigate_to("subdir");

    REQUIRE_FALSE(nav.is_at_root());
}

TEST_CASE("[PathNavigator] reset returns to root") {
    PrintSelectPathNavigator nav;
    nav.navigate_to("a");
    nav.navigate_to("b");
    nav.navigate_to("c");
    REQUIRE(nav.current_path() == "a/b/c");
    REQUIRE_FALSE(nav.is_at_root());

    nav.reset();

    REQUIRE(nav.current_path().empty());
    REQUIRE(nav.is_at_root());
}
