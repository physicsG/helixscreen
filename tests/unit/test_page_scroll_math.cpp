// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#include "page_scroll_math.h"

#include "../catch_amalgamated.hpp"

using namespace helix::ui;

TEST_CASE("page_scroll_step is ~90% of viewport, floored", "[page_scroll_buttons]") {
    CHECK(page_scroll_step(200) == 180);
    CHECK(page_scroll_step(100) == 90);
    CHECK(page_scroll_step(1) == 1); // floored to 1, never 0 for positive
    CHECK(page_scroll_step(0) == 0);
    CHECK(page_scroll_step(-50) == 0); // guard against nonsense
}

TEST_CASE("page_scroll_compute_reach flags ends and overflow", "[page_scroll_buttons]") {
    SECTION("top of an overflowing list") {
        auto r = page_scroll_compute_reach(/*scroll_y=*/0, /*scroll_bottom=*/300);
        CHECK_FALSE(r.can_up);
        CHECK(r.can_down);
        CHECK(r.has_overflow);
    }
    SECTION("bottom of an overflowing list") {
        auto r = page_scroll_compute_reach(300, 0);
        CHECK(r.can_up);
        CHECK_FALSE(r.can_down);
        CHECK(r.has_overflow);
    }
    SECTION("middle") {
        auto r = page_scroll_compute_reach(150, 150);
        CHECK(r.can_up);
        CHECK(r.can_down);
        CHECK(r.has_overflow);
    }
    SECTION("content fits — no overflow") {
        auto r = page_scroll_compute_reach(0, 0);
        CHECK_FALSE(r.can_up);
        CHECK_FALSE(r.can_down);
        CHECK_FALSE(r.has_overflow);
    }
}
