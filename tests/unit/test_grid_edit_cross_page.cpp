// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_grid_edit_cross_page.cpp
 * @brief The pure cross-page drag rules: edge push, majority crossing, edge
 * dwell and drop-creates-page (prestonbrown/helixscreen#1638).
 */

#include "grid_edit_cross_page.h"

#include <cstdlib>
#include <initializer_list>

#include "../catch_amalgamated.hpp"

using helix::cross_page_drop_creates_page;
using helix::cross_page_edge_zone_px;
using helix::cross_page_note_dwell_flip;
using helix::cross_page_past_left_border;
using helix::cross_page_past_right_border;
using helix::CROSS_PAGE_PUSH_CAP_SLOP_PX;
using helix::cross_page_push_px_per_s;
using helix::cross_page_step;
using helix::CrossPageInput;
using helix::CrossPageState;
using helix::CrossPageStep;

namespace {

/// The 800x480 panel's column track (docs/devel/LAYOUT_SYSTEM.md, "Measured grids").
constexpr float TRACK_800x480 = 54.58f;

/// A 700px page frame, off the screen origin so no rule can pass by reading 0
/// for an edge.
constexpr int FRAME_X1 = 10;
constexpr int FRAME_X2 = 709;

/// A two-track widget, and one wide enough that its leading side crosses a
/// border while the pointer is still clear of the edge zone.
constexpr int WIDGET_W = 120;
constexpr int WIDE_W = 400;

CrossPageInput cross_move(int pointer_x, int widget_left, uint32_t elapsed_ms,
                          int widget_w = WIDGET_W) {
    CrossPageInput in;
    in.frame_x1 = FRAME_X1;
    in.frame_x2 = FRAME_X2;
    in.cell_w = TRACK_800x480;
    in.pointer_x = pointer_x;
    in.widget_left = widget_left;
    in.widget_width = widget_w;
    in.elapsed_ms = elapsed_ms;
    in.page_index = 0;
    in.page_count = 2;
    in.has_next_page_slot = true;
    return in;
}

int zone_px() {
    return cross_page_edge_zone_px(TRACK_800x480);
}

} // namespace

TEST_CASE("cross-page tuning keeps the 800x480 feel and scales with the track",
          "[1638][grid_edit][cross_page]") {
    CHECK(zone_px() == 40);
    const int speed = cross_page_push_px_per_s(TRACK_800x480);
    CHECK(speed >= 175);
    CHECK(speed <= 185);

    // A track twice as wide doubles both, within truncation.
    CHECK(std::abs(cross_page_edge_zone_px(2 * TRACK_800x480) - 2 * zone_px()) <= 1);
    CHECK(std::abs(cross_page_push_px_per_s(2 * TRACK_800x480) - 2 * speed) <= 1);

    // Before a grid exists there is no track: the 800x480 tuning stands in.
    CHECK(cross_page_edge_zone_px(0.0f) == zone_px());
    CHECK(cross_page_edge_zone_px(-3.0f) == zone_px());
    CHECK(cross_page_push_px_per_s(0.0f) == speed);

    // Vanishing tracks share one floor instead of shrinking the zone with them.
    CHECK(cross_page_edge_zone_px(1.0f) == cross_page_edge_zone_px(8.0f));
    CHECK(cross_page_edge_zone_px(8.0f) > static_cast<int>(8.0f));
}

TEST_CASE("the edge push advances by time in the zone rather than by move count",
          "[1638][grid_edit][cross_page]") {
    const int speed = cross_page_push_px_per_s(TRACK_800x480);
    const int x = FRAME_X2 - 20;
    const int raw_left = x - 110; // grabbed near the widget's right side
    REQUIRE(x >= FRAME_X2 - zone_px());

    // Pushed px after the zone entry and then moves @p steps ms apart.
    auto push_after = [&](std::initializer_list<uint32_t> steps) {
        CrossPageState state;
        CrossPageStep out = cross_page_step(state, cross_move(x, raw_left, 0));
        REQUIRE(out.widget_left == raw_left);
        for (uint32_t ms : steps) {
            out = cross_page_step(state, cross_move(x, raw_left, ms));
        }
        return out.widget_left - raw_left;
    };

    const int pushed_66ms = speed * 66 / 1000;
    REQUIRE(pushed_66ms > 0);
    REQUIRE(raw_left + pushed_66ms <= FRAME_X2 - WIDGET_W / 2); // clear of crossing and cap

    CHECK(push_after({66}) == pushed_66ms);
    CHECK(push_after({33, 33}) == pushed_66ms);
    CHECK(push_after({11, 11, 11, 11, 11, 11}) == pushed_66ms);
    CHECK(push_after({0, 0, 0, 0}) == 0);

    SECTION("time before entering the zone pushes nothing, and leaving drops the push") {
        CrossPageState state;
        const int mid = (FRAME_X1 + FRAME_X2) / 2;
        cross_page_step(state, cross_move(mid, mid - 110, 0));
        CHECK(cross_page_step(state, cross_move(x, raw_left, 1000)).widget_left == raw_left);
        CHECK(cross_page_step(state, cross_move(x, raw_left, 66)).widget_left ==
              raw_left + pushed_66ms);
        CHECK(cross_page_step(state, cross_move(mid, mid - 110, 33)).widget_left == mid - 110);
        CHECK(cross_page_step(state, cross_move(x, raw_left, 1000)).widget_left == raw_left);
    }
}

TEST_CASE("the push caps mirror on both sides and both reach a crossing",
          "[1638][grid_edit][cross_page]") {
    SECTION("right: the cap sits the slop past the majority line") {
        const int x = FRAME_X2 - zone_px() / 2;
        const int raw_left = x - 110; // majority inside: only the push can cross
        const int majority_line = FRAME_X2 - WIDGET_W / 2;
        REQUIRE(raw_left < majority_line);
        CrossPageState state;
        cross_page_step(state, cross_move(x, raw_left, 0));
        const CrossPageStep out = cross_page_step(state, cross_move(x, raw_left, 10000));
        CHECK(out.widget_left - majority_line == CROSS_PAGE_PUSH_CAP_SLOP_PX);
        CHECK(out.flip_dir == 1);
    }

    SECTION("left: the mirror of the right cap, so a left crossing is reachable") {
        const int x = FRAME_X1 + zone_px() / 2;
        const int raw_left = x - 10; // majority inside: only the push can cross
        const int majority_line = FRAME_X1 - WIDGET_W / 2;
        REQUIRE(raw_left > majority_line);
        CrossPageState state;
        cross_page_step(state, cross_move(x, raw_left, 0));
        const CrossPageStep out = cross_page_step(state, cross_move(x, raw_left, 10000));
        CHECK(majority_line - out.widget_left == CROSS_PAGE_PUSH_CAP_SLOP_PX);
        CHECK(out.flip_dir == -1);
    }

    SECTION("a pointer that already carries the widget past the cap is held at it") {
        const int x = FRAME_X2 - zone_px() / 2;
        const int raw_left = x - 10;
        const int cap = FRAME_X2 - WIDGET_W / 2 + CROSS_PAGE_PUSH_CAP_SLOP_PX;
        REQUIRE(raw_left > cap);
        CrossPageState state;
        CHECK(cross_page_step(state, cross_move(x, raw_left, 0)).widget_left == cap);
        CHECK(cross_page_step(state, cross_move(x, raw_left, 500)).widget_left == cap);
    }
}

TEST_CASE("a crossing requests one flip and re-arms once the widget is back inside",
          "[1638][grid_edit][cross_page]") {
    // A wide widget crossed by its leading side, the pointer clear of both
    // zones: nothing but the crossing can ask for a flip.
    SECTION("right") {
        const int x = FRAME_X2 - zone_px() - 20;
        const int crossed_left = FRAME_X2 - WIDE_W / 2 + 30;
        const int inside_left = FRAME_X2 - WIDE_W / 2 - 30;
        REQUIRE(x > crossed_left);
        REQUIRE(x - 60 > inside_left);
        CrossPageState state;
        CHECK(cross_page_step(state, cross_move(x, crossed_left, 33, WIDE_W)).flip_dir == 1);
        CHECK(cross_page_step(state, cross_move(x, crossed_left, 33, WIDE_W)).flip_dir == 0);
        CHECK(cross_page_step(state, cross_move(x - 20, crossed_left - 20, 33, WIDE_W)).flip_dir ==
              0);
        CHECK(cross_page_step(state, cross_move(x - 60, inside_left, 33, WIDE_W)).flip_dir == 0);
        CHECK(cross_page_step(state, cross_move(x, crossed_left, 33, WIDE_W)).flip_dir == 1);
    }

    SECTION("left") {
        const int x = FRAME_X1 + zone_px() + 20;
        const int crossed_left = FRAME_X1 - WIDE_W / 2 - 30;
        const int inside_left = FRAME_X1 - WIDE_W / 2 + 30;
        REQUIRE(x < crossed_left + WIDE_W);
        REQUIRE(x + 60 < inside_left + WIDE_W);
        CrossPageState state;
        CHECK(cross_page_step(state, cross_move(x, crossed_left, 33, WIDE_W)).flip_dir == -1);
        CHECK(cross_page_step(state, cross_move(x, crossed_left, 33, WIDE_W)).flip_dir == 0);
        CHECK(cross_page_step(state, cross_move(x + 60, inside_left, 33, WIDE_W)).flip_dir == 0);
        CHECK(cross_page_step(state, cross_move(x, crossed_left, 33, WIDE_W)).flip_dir == -1);
    }
}

TEST_CASE("the dwell starts on zone entry and stops on zone exit",
          "[1638][grid_edit][cross_page]") {
    const int right_x = FRAME_X2 - 20;
    const int left_x = FRAME_X1 + 20;
    const int mid = (FRAME_X1 + FRAME_X2) / 2;
    CrossPageState state;

    CrossPageStep out = cross_page_step(state, cross_move(right_x, right_x - 110, 0));
    CHECK(out.dwell_dir == 1);
    CHECK(out.dwell_changed);

    out = cross_page_step(state, cross_move(right_x, right_x - 110, 0));
    CHECK(out.dwell_dir == 1);
    CHECK_FALSE(out.dwell_changed); // still counting: a restart would never complete

    out = cross_page_step(state, cross_move(mid, mid - 60, 33));
    CHECK(out.dwell_dir == 0);
    CHECK(out.dwell_changed);

    out = cross_page_step(state, cross_move(left_x, left_x - 10, 33));
    CHECK(out.dwell_dir == -1);
    CHECK(out.dwell_changed);

    out = cross_page_step(state, cross_move(right_x, right_x - 110, 33));
    CHECK(out.dwell_dir == 1);
    CHECK(out.dwell_changed);
    CHECK(out.flip_dir == 0);
}

TEST_CASE("a flip spends the zone stay for both triggers", "[1638][grid_edit][cross_page]") {
    const int x = FRAME_X2 - 20;
    const int raw_left = x - 110; // majority inside: the push crosses after a while
    const int mid = (FRAME_X1 + FRAME_X2) / 2;

    SECTION("a crossing in the zone stops the dwell, and holding asks for nothing more") {
        CrossPageState state;
        REQUIRE(cross_page_step(state, cross_move(x, raw_left, 0)).dwell_dir == 1);
        int flips = 0;
        bool dwell_stopped_at_crossing = false;
        for (int held_ms = 0; held_ms < 1500; held_ms += 33) {
            const CrossPageStep out = cross_page_step(state, cross_move(x, raw_left, 33));
            if (out.flip_dir != 0) {
                ++flips;
                dwell_stopped_at_crossing = out.dwell_dir == 0 && out.dwell_changed;
            }
            if (flips > 0) {
                CHECK(out.dwell_dir == 0);
            }
        }
        CHECK(flips == 1);
        CHECK(dwell_stopped_at_crossing);
    }

    SECTION("a dwell flip stops the push's later crossing in the same zone stay") {
        CrossPageState state;
        REQUIRE(cross_page_step(state, cross_move(x, raw_left, 0)).dwell_dir == 1);
        cross_page_note_dwell_flip(state);
        bool crossed = false;
        for (int held_ms = 0; held_ms < 1500; held_ms += 33) {
            const CrossPageStep out = cross_page_step(state, cross_move(x, raw_left, 33));
            crossed |= out.widget_left > FRAME_X2 - WIDGET_W / 2;
            CHECK(out.flip_dir == 0);
            CHECK(out.dwell_dir == 0);
        }
        CHECK(crossed); // the push did carry the widget over: the crossing was suppressed
    }

    SECTION("leaving the zone with the widget inside makes both available again") {
        CrossPageState state;
        cross_page_step(state, cross_move(x, raw_left, 0));
        cross_page_note_dwell_flip(state);
        CHECK(cross_page_step(state, cross_move(mid, mid - 60, 33)).flip_dir == 0);
        const CrossPageStep back = cross_page_step(state, cross_move(x, raw_left, 33));
        CHECK(back.dwell_dir == 1);
        CHECK(back.dwell_changed);
    }
}

TEST_CASE("a drop creates a page anywhere on the page past the last, from the last page only "
          "past its right border, and from the first page past its left border",
          "[1638][grid_edit][cross_page]") {
    const int x = FRAME_X2 - zone_px() - 20;
    const int past_right = FRAME_X2 - WIDE_W / 2 + 30;
    const int inside = FRAME_X2 - WIDE_W / 2 - 30;
    const int past_left = FRAME_X1 - WIDE_W / 2 - 30;
    const int left_of_zone = FRAME_X1 + zone_px() + 20;

    struct Row {
        const char* what;
        int widget_left;
        int pointer_x;
        int page_index;
        bool has_next_page_slot;
        bool creates;
    };
    const Row rows[] = {
        {"majority right on the last page", past_right, x, 1, true, true},
        {"majority inside on the last page", inside, x - 60, 1, true, false},
        {"majority left on the last page", past_left, left_of_zone, 1, true, false},
        {"majority right on an earlier page", past_right, x, 0, true, false},
        {"majority right on the last page with no page to create", past_right, x, 1, false, false},
        {"majority right on the page past the last", past_right, x, 2, true, true},
        {"majority inside on the page past the last", inside, x - 60, 2, true, true},
        {"majority left on the page past the last", past_left, left_of_zone, 2, true, true},
        {"inside the page past the last with no page to create", inside, x - 60, 2, false, false},
        {"majority left on the first page", past_left, left_of_zone, 0, true, true},
        {"majority inside on the first page", inside, x - 60, 0, true, false},
        {"majority right on the first page", past_right, x, 0, true, false},
        {"majority left on the first page with no page to create", past_left, left_of_zone, 0,
         false, false},
    };
    for (const Row& row : rows) {
        INFO(row.what);
        CrossPageInput in = cross_move(row.pointer_x, row.widget_left, 33, WIDE_W);
        in.page_index = row.page_index;
        in.page_count = 2;
        in.has_next_page_slot = row.has_next_page_slot;
        CrossPageState state;
        const CrossPageStep step = cross_page_step(state, in);
        const bool past_right =
            cross_page_past_right_border(in.frame_x2, step.widget_left, in.widget_width);
        const bool past_left_border =
            cross_page_past_left_border(in.frame_x1, step.widget_left, in.widget_width);
        CHECK(cross_page_drop_creates_page(in.page_index, in.page_count, in.has_next_page_slot,
                                           past_right, past_left_border) == row.creates);
    }
}
