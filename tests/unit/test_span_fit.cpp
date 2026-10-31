// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_span_fit.cpp
 * @brief grow_span_to_fit finds the nearest span a widget can actually draw.
 *
 * The resize clamp and the load path both ask this, so a break here reaches
 * both. Run with: ./build/bin/helix-tests "[span_fit]"
 */

#include "grid_layout.h"

#include "../catch_amalgamated.hpp"

using namespace helix;

namespace {

/// One cell = 2 tracks of 50px with a 4px gutter, so span 2 is 104px.
constexpr CellMetrics kMetrics{50.0f, 50.0f, 4};

int extent(int span) {
    return static_cast<int>(grid_track_extent(kMetrics.cell_w, kMetrics.gutter, span));
}

/// Accepts any box at least `min_w` x `min_h`. Monotonic, as fits_at must be.
auto min_box(int min_w, int min_h) {
    return [min_w, min_h](int w, int h) { return w >= min_w && h >= min_h; };
}

} // namespace

TEST_CASE("a span that already fits is returned untouched", "[span_fit][1559]") {
    auto [c, r] = grow_span_to_fit(min_box(1, 1), 2, 2, 64, 64, 2, 2, kMetrics);
    CHECK(c == 2);
    CHECK(r == 2);
}

TEST_CASE("a too-small span grows to the nearest accepting one", "[span_fit][1559]") {
    // Needs 150px wide. Span 2 is 104, span 4 is 212: the first acceptance.
    REQUIRE(extent(2) == 104);
    REQUIRE(extent(4) == 212);
    auto [c, r] = grow_span_to_fit(min_box(150, 1), 2, 2, 64, 64, 2, 2, kMetrics);
    CHECK(c == 4);
    CHECK(r == 2); // the axis that already fits is not inflated past its need
}

TEST_CASE("growth stops at the registry maximum", "[span_fit][1559]") {
    // Nothing this widget can be given is wide enough; it must stop at max
    // rather than loop or run past the grid.
    auto [c, r] = grow_span_to_fit(min_box(100000, 1), 2, 2, 8, 8, 2, 2, kMetrics);
    CHECK(c == 8);
    CHECK(r == 8);
}

TEST_CASE("growth lands on the widget's snap step", "[span_fit][1559]") {
    // A whole-cell widget steps by 2 tracks, so it can never come to rest on an
    // odd span the placement engine would refuse to seat.
    auto [c, r] = grow_span_to_fit(min_box(150, 1), 2, 2, 64, 64, 2, 2, kMetrics);
    CHECK(c % 2 == 0);
    auto [hc, hr] = grow_span_to_fit(min_box(150, 1), 1, 1, 64, 64, 1, 1, kMetrics);
    CHECK(hc == 3); // a half-cell widget may rest on an odd span
    CHECK(extent(3) >= 150);
    CHECK(extent(2) < 150);
    (void)r;
    (void)hr;
}

TEST_CASE("a widget that accepts everything never grows", "[span_fit][1559]") {
    // PanelWidget::fits_at defaults to true, so an un-opted-in widget must keep
    // exactly the span it was given.
    auto always = [](int, int) { return true; };
    for (int span = 1; span <= 16; ++span) {
        auto [c, r] = grow_span_to_fit(always, span, span, 64, 64, 1, 1, kMetrics);
        INFO("span " << span);
        CHECK(c == span);
        CHECK(r == span);
    }
}

// ---------------------------------------------------------------------------
// seat_grown_span: where a grown widget may sit without taking a neighbour's
// tracks. Run with: ./build/bin/helix-tests "[seat_grown]"
// ---------------------------------------------------------------------------

namespace {

bool overlaps(const TrackRect& a, const TrackRect& b) {
    return a.col < b.col + b.colspan && b.col < a.col + a.colspan && a.row < b.row + b.rowspan &&
           b.row < a.row + a.rowspan;
}

/// Accepts a candidate that stays clear of every rectangle in @p taken.
auto clear_of(std::vector<TrackRect> taken) {
    return [taken](const TrackRect& c) {
        for (const auto& t : taken) {
            if (overlaps(c, t)) {
                return false;
            }
        }
        return true;
    };
}

} // namespace

TEST_CASE("seat_grown_span: growth into free space extends right", "[span_fit][seat_grown]") {
    const TrackRect saved{4, 0, 4, 4};
    const auto r = seat_grown_span(saved, 5, 4, 12, 8, 1, 1, clear_of({}));
    CHECK(r.col == 4);
    CHECK(r.row == 0);
    CHECK(r.colspan == 5);
    CHECK(r.rowspan == 4);
}

TEST_CASE("seat_grown_span: a neighbour on the right moves the growth left",
          "[span_fit][seat_grown]") {
    // Tracks 0..3 free, a neighbour flush against the right edge at 8.
    const TrackRect saved{4, 0, 4, 4};
    const TrackRect neighbour{8, 0, 4, 2};
    const auto r = seat_grown_span(saved, 5, 4, 12, 8, 1, 1, clear_of({neighbour}));
    CHECK(r.col == 3);
    CHECK(r.colspan == 5);
    CHECK_FALSE(overlaps(r, neighbour));
}

TEST_CASE("seat_grown_span: boxed in on both sides keeps the saved span",
          "[span_fit][seat_grown]") {
    // The 1.0 home layout at 800x480: tips between printer_image and active_spool.
    const TrackRect saved{4, 0, 4, 4};
    const TrackRect left{0, 0, 4, 4};
    const TrackRect right{8, 0, 4, 2};
    const auto r = seat_grown_span(saved, 5, 4, 12, 8, 1, 1, clear_of({left, right}));
    CHECK(r.col == saved.col);
    CHECK(r.row == saved.row);
    CHECK(r.colspan == saved.colspan);
    CHECK(r.rowspan == saved.rowspan);
}

TEST_CASE("seat_grown_span: the shifted origin lands on the snap step", "[span_fit][seat_grown]") {
    // A whole-cell widget grown 2 -> 4 against the right edge shifts by 2, never 1.
    const TrackRect saved{2, 0, 2, 2};
    const auto r = seat_grown_span(saved, 4, 2, 4, 4, 2, 2, clear_of({}));
    CHECK(r.col == 0);
    CHECK(r.colspan == 4);
}

TEST_CASE("seat_grown_span: growth never leaves the grid", "[span_fit][seat_grown]") {
    // Grown taller than the rows below allow: the origin moves up instead.
    const TrackRect saved{0, 6, 2, 2};
    const auto r = seat_grown_span(saved, 2, 4, 12, 8, 1, 1, clear_of({}));
    CHECK(r.row == 4);
    CHECK(r.rowspan == 4);
    CHECK(r.row + r.rowspan <= 8);
}
