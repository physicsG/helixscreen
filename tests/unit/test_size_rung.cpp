// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_size_rung.cpp
 * @brief choose_size_rung() at every rung boundary, one pixel either side.
 *
 * Run with: ./build/bin/helix-tests "[size_rung]"
 */

#include "src/ui/panel_widgets/size_rung.h"

#include "../catch_amalgamated.hpp"

using namespace helix;

namespace {

/// A widget that offers every rung up to Full.
constexpr RungNeeds kLadder{rung_box(40, 40),   rung_box(100, 40),  rung_box(40, 100),
                            rung_box(100, 100), rung_box(200, 100), rung_box(200, 200)};

} // namespace

TEST_CASE("choose_size_rung: every boundary flips on the exact pixel", "[size_rung]") {
    struct Row {
        int w;
        int h;
        SizeRung expected;
    };
    const Row rows[] = {
        {40, 40, SizeRung::Glance},
        {39, 40, SizeRung::Glance},
        {40, 39, SizeRung::Glance},
        {100, 40, SizeRung::Strip},
        {99, 40, SizeRung::Glance},
        {100, 39, SizeRung::Glance},
        {40, 100, SizeRung::Column},
        {39, 100, SizeRung::Glance},
        {40, 99, SizeRung::Glance},
        {100, 100, SizeRung::Cell},
        {99, 100, SizeRung::Column},
        {100, 99, SizeRung::Strip},
        {200, 100, SizeRung::Wide},
        {199, 100, SizeRung::Cell},
        {200, 99, SizeRung::Strip},
        {200, 200, SizeRung::Full},
        {199, 200, SizeRung::Cell},
        {200, 199, SizeRung::Wide},
        {500, 500, SizeRung::Full},
        // Between rungs (1.5x1, 1x2): the largest rung the box holds.
        {150, 100, SizeRung::Cell},
        {100, 160, SizeRung::Cell},
    };
    for (const auto& r : rows) {
        INFO(r.w << "x" << r.h);
        CHECK(choose_size_rung(r.w, r.h, kLadder) == r.expected);
    }
}

TEST_CASE("choose_size_rung: a box holding both Strip and Column takes its aspect", "[size_rung]") {
    // No Cell rung, so Strip and Column compete.
    constexpr RungNeeds needs{rung_box(10, 10), rung_box(50, 30), rung_box(30, 50)};
    CHECK(choose_size_rung(60, 55, needs) == SizeRung::Strip);
    CHECK(choose_size_rung(55, 60, needs) == SizeRung::Column);
    CHECK(choose_size_rung(60, 60, needs) == SizeRung::Strip);
}

TEST_CASE(
    "choose_size_rung: a box that holds only one of Strip and Column takes it whatever its aspect",
    "[size_rung]") {
    constexpr RungNeeds strip_only{rung_box(10, 10), rung_box(50, 30)};
    CHECK(choose_size_rung(55, 90, strip_only) == SizeRung::Strip);
    constexpr RungNeeds column_only{rung_box(10, 10), RungBox{}, rung_box(30, 50)};
    CHECK(choose_size_rung(90, 55, column_only) == SizeRung::Column);
}

TEST_CASE("choose_size_rung: a rung the widget does not offer is never chosen", "[size_rung]") {
    constexpr RungNeeds needs{rung_box(10, 10), RungBox{}, RungBox{},
                              RungBox{},        RungBox{}, rung_box(100, 100)};
    CHECK(choose_size_rung(99, 99, needs) == SizeRung::Glance);
    CHECK(choose_size_rung(100, 100, needs) == SizeRung::Full);
    // Large is only chosen when offered.
    CHECK(choose_size_rung(900, 900, needs) == SizeRung::Full);
    constexpr RungNeeds with_large{rung_box(10, 10),  RungBox{}, RungBox{},
                                   RungBox{},         RungBox{}, rung_box(100, 100),
                                   rung_box(300, 300)};
    CHECK(choose_size_rung(299, 300, with_large) == SizeRung::Full);
    CHECK(choose_size_rung(300, 300, with_large) == SizeRung::Large);
}

TEST_CASE("holds_any_rung: false below the smallest offered rung, monotonic above", "[size_rung]") {
    CHECK_FALSE(holds_any_rung(39, 40, kLadder));
    CHECK_FALSE(holds_any_rung(40, 39, kLadder));
    CHECK(holds_any_rung(40, 40, kLadder));

    bool fitted = false;
    for (int w = 0; w <= 120; ++w) {
        const bool now = holds_any_rung(w, 120, kLadder);
        if (fitted) {
            CHECK(now);
        }
        fitted = fitted || now;
    }
    CHECK(fitted);
    // Nothing offered: nothing fits.
    CHECK_FALSE(holds_any_rung(1000, 1000, RungNeeds{}));
}
