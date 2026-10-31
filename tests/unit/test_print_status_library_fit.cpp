// Copyright (C) 2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_print_status_library_fit.cpp
 * @brief The idle library card's action buttons: labelled column, icon-only
 *        column, or two rows of icons.
 */

#include "src/ui/panel_widgets/print_status_library_layout.h"

#include "../catch_amalgamated.hpp"

using namespace helix;

namespace {

constexpr int kGap = 4;
constexpr int kMinButtonH = 40;

LibraryActionsInput input(int w, int h, std::initializer_list<int> labeled) {
    LibraryActionsInput in;
    in.avail_w = w;
    in.avail_h = h;
    in.gap = kGap;
    in.min_button_h = kMinButtonH;
    for (int need : labeled) {
        in.labeled_w[in.count++] = need;
    }
    return in;
}

} // namespace

TEST_CASE("library actions: four buttons in a 4x2 card go to two rows of icons",
          "[print_status_library_fit]") {
    // 120x59: each stacked button would be ~10px tall.
    const auto v = decide_library_actions_layout(input(120, 59, {80, 90, 95, 85}));
    CHECK(v.grid);
    CHECK(v.icon_only);
    CHECK(v.second_row_at == 2);
}

TEST_CASE("library actions: a tall column with a label too wide drops every label, stays a column",
          "[print_status_library_fit]") {
    // German-width "Auftragswarteschlange" beside its icon needs more than 120.
    const auto v = decide_library_actions_layout(input(120, 300, {100, 110, 105, 150}));
    CHECK_FALSE(v.grid);
    CHECK(v.icon_only);
}

TEST_CASE("library actions: tall and wide enough keeps the labelled column",
          "[print_status_library_fit]") {
    const auto v = decide_library_actions_layout(input(200, 300, {100, 110, 105, 150}));
    CHECK_FALSE(v.grid);
    CHECK_FALSE(v.icon_only);
}

TEST_CASE("library actions: the visible count decides the stacked height",
          "[print_status_library_fit]") {
    // 3 buttons need 3*40 + 2*4 = 128; 2 need 2*40 + 4 = 84.
    SECTION("three buttons: two on top, one below") {
        const auto short_v = decide_library_actions_layout(input(200, 127, {80, 80, 80}));
        CHECK(short_v.grid);
        CHECK(short_v.icon_only);
        CHECK(short_v.second_row_at == 2);
        const auto tall_v = decide_library_actions_layout(input(200, 128, {80, 80, 80}));
        CHECK_FALSE(tall_v.grid);
        CHECK_FALSE(tall_v.icon_only);
        CHECK(tall_v.second_row_at == -1);
    }
    SECTION("two buttons stay one per row, icons only when short") {
        const auto short_v = decide_library_actions_layout(input(200, 83, {80, 80}));
        CHECK_FALSE(short_v.grid);
        CHECK(short_v.icon_only);
        CHECK(short_v.second_row_at == -1);
        const auto v = decide_library_actions_layout(input(200, 84, {80, 80}));
        CHECK_FALSE(v.grid);
        CHECK_FALSE(v.icon_only);
    }
    SECTION("one button: icon only when short") {
        CHECK(decide_library_actions_layout(input(200, 39, {80})).icon_only);
        CHECK_FALSE(decide_library_actions_layout(input(200, 40, {80})).icon_only);
        CHECK_FALSE(decide_library_actions_layout(input(200, 39, {80})).grid);
    }
    SECTION("the same height holds two buttons but not three") {
        CHECK_FALSE(decide_library_actions_layout(input(200, 100, {80, 80})).icon_only);
        CHECK(decide_library_actions_layout(input(200, 100, {80, 80, 80})).grid);
    }
}

TEST_CASE("library actions: boundary at exactly button_height_sm per button",
          "[print_status_library_fit]") {
    // 4 buttons: 4*40 + 3*4 = 172.
    CHECK_FALSE(decide_library_actions_layout(input(200, 172, {80, 80, 80, 80})).grid);
    CHECK(decide_library_actions_layout(input(200, 171, {80, 80, 80, 80})).grid);
}

TEST_CASE("library actions: boundary at exactly the label's fit width",
          "[print_status_library_fit]") {
    CHECK_FALSE(decide_library_actions_layout(input(120, 300, {80, 120, 90})).icon_only);
    CHECK(decide_library_actions_layout(input(120, 300, {80, 121, 90})).icon_only);
}

TEST_CASE("library actions: nothing visible or nothing measured keeps the labelled column",
          "[print_status_library_fit]") {
    const auto none = decide_library_actions_layout(input(120, 59, {}));
    CHECK_FALSE(none.grid);
    CHECK_FALSE(none.icon_only);
    const auto unmeasured = decide_library_actions_layout(input(0, 0, {80, 80, 80, 80}));
    CHECK_FALSE(unmeasured.grid);
    CHECK_FALSE(unmeasured.icon_only);
}
