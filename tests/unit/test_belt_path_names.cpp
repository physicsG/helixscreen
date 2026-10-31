// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_belt_path_names.cpp
 * @brief What the Belt Tension screens call each path, for every saved layout.
 */

#include "ui_belt_path_names.h"

#include "belt_tension_types.h"

#include <string>

#include "../catch_amalgamated.hpp"

using helix::calibration::belt_layout_from_int;
using helix::calibration::BeltLayout;
using helix::calibration::BeltPath;
using helix::ui::belt_path_in_slot;
using helix::ui::belt_path_name;

namespace {

struct Expected {
    BeltLayout layout;
    const char* a_title;
    const char* a_phrase;
    const char* b_title;
    const char* b_phrase;
};

} // namespace

TEST_CASE("belt path names follow the saved layout", "[belt][names]") {
    const Expected rows[] = {
        {BeltLayout::UNSET, "Path A", "Path A", "Path B", "Path B"},
        {BeltLayout::A_LEFT_B_RIGHT, "Left belt", "the left belt", "Right belt", "the right belt"},
        {BeltLayout::A_RIGHT_B_LEFT, "Right belt", "the right belt", "Left belt", "the left belt"},
        {BeltLayout::A_UPPER_B_LOWER, "Upper belt", "the upper belt", "Lower belt",
         "the lower belt"},
        {BeltLayout::A_LOWER_B_UPPER, "Lower belt", "the lower belt", "Upper belt",
         "the upper belt"},
    };
    for (const auto& row : rows) {
        INFO("layout " << static_cast<int>(row.layout));
        const auto a = belt_path_name(BeltPath::PATH_A, row.layout);
        const auto b = belt_path_name(BeltPath::PATH_B, row.layout);
        CHECK(a.title == row.a_title);
        CHECK(a.phrase == row.a_phrase);
        CHECK(b.title == row.b_title);
        CHECK(b.phrase == row.b_phrase);
    }
}

TEST_CASE("a stored belt layout out of range reads as not set", "[belt][names]") {
    CHECK(belt_layout_from_int(0) == BeltLayout::UNSET);
    CHECK(belt_layout_from_int(1) == BeltLayout::A_LEFT_B_RIGHT);
    CHECK(belt_layout_from_int(4) == BeltLayout::A_LOWER_B_UPPER);
    CHECK(belt_layout_from_int(helix::calibration::kBeltLayoutCount) == BeltLayout::UNSET);
    CHECK(belt_layout_from_int(-1) == BeltLayout::UNSET);
}

TEST_CASE("RESULTS puts the paths in physical order only when A is the right belt",
          "[belt][names]") {
    // A_RIGHT_B_LEFT is the one layout where A-then-B would read backwards.
    CHECK(belt_path_in_slot(0, BeltLayout::A_RIGHT_B_LEFT) == BeltPath::PATH_B);
    CHECK(belt_path_in_slot(1, BeltLayout::A_RIGHT_B_LEFT) == BeltPath::PATH_A);
    for (const auto layout : {BeltLayout::UNSET, BeltLayout::A_LEFT_B_RIGHT,
                              BeltLayout::A_UPPER_B_LOWER, BeltLayout::A_LOWER_B_UPPER}) {
        INFO("layout " << static_cast<int>(layout));
        CHECK(belt_path_in_slot(0, layout) == BeltPath::PATH_A);
        CHECK(belt_path_in_slot(1, layout) == BeltPath::PATH_B);
    }
}
