// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_exclude_map_layout.cpp
 * @brief decide_exclude_map_layout() at each rung boundary for the map and the
 * placeholder, classify_exclude_map(), and count_objects().
 *
 * The needs below are boxes of the shape ExcludeMapWidget measures at the medium tier;
 * the widget tests pin the real measured values.
 *
 * Run with: ./build/bin/helix-tests "[exclude_map][layout]"
 */

#include "src/ui/panel_widgets/exclude_map_layout.h"

#include "../catch_amalgamated.hpp"

using namespace helix;

namespace {

constexpr RungNeeds kObjects{rung_box(30, 28), rung_box(60, 24),  rung_box(32, 70),
                             rung_box(90, 60), rung_box(150, 62), rung_box(120, 150)};
constexpr RungNeeds kPlaceholder{rung_box(16, 16), rung_box(60, 34), {}, {}, {}, {}, {}};
constexpr int kFullMinH = 160;

ExcludeMapVerdict map_verdict(int w, int h, int full_min_h = 0) {
    return decide_exclude_map_layout({w, h, ExcludeMapState::Objects, kObjects, full_min_h});
}

ExcludeMapVerdict placeholder_verdict(int w, int h, ExcludeMapState state) {
    return decide_exclude_map_layout({w, h, state, kPlaceholder, kFullMinH});
}

} // namespace

TEST_CASE("exclude_map layout: map rung boundaries are exact", "[exclude_map][layout]") {
    CHECK(map_verdict(30, 28).rung == SizeRung::Glance);
    CHECK(map_verdict(60, 24).rung == SizeRung::Strip);
    CHECK(map_verdict(59, 40).rung == SizeRung::Glance);
    CHECK(map_verdict(60, 23).rung == SizeRung::Glance);
    CHECK(map_verdict(32, 70).rung == SizeRung::Column);
    CHECK(map_verdict(32, 69).rung == SizeRung::Glance);
    CHECK(map_verdict(90, 60).rung == SizeRung::Cell);
    CHECK(map_verdict(89, 60).rung == SizeRung::Strip);
    CHECK(map_verdict(90, 59).rung == SizeRung::Strip);
    CHECK(map_verdict(150, 62).rung == SizeRung::Wide);
    CHECK(map_verdict(149, 62).rung == SizeRung::Cell);
    CHECK(map_verdict(150, 61).rung == SizeRung::Cell);
    CHECK(map_verdict(120, 150).rung == SizeRung::Full);
    CHECK(map_verdict(119, 150).rung == SizeRung::Cell);
    CHECK(map_verdict(120, 149).rung == SizeRung::Cell);
}

TEST_CASE("exclude_map layout: Full also needs the 2x2 height", "[exclude_map][layout]") {
    CHECK(map_verdict(200, 150, kFullMinH).rung == SizeRung::Wide);
    CHECK(map_verdict(200, 159, kFullMinH).rung == SizeRung::Wide);
    CHECK(map_verdict(120, 159, kFullMinH).rung == SizeRung::Cell);
    CHECK(map_verdict(200, 160, kFullMinH).rung == SizeRung::Full);
    CHECK(map_verdict(119, 300, kFullMinH).rung == SizeRung::Cell);
}

TEST_CASE("exclude_map layout: the map and its extras follow the rung", "[exclude_map][layout]") {
    struct Row {
        int w, h;
        SizeRung rung;
        bool map, skip, name;
    };
    const Row rows[] = {
        {30, 28, SizeRung::Glance, false, false, false},
        {60, 24, SizeRung::Strip, false, false, false},
        {32, 70, SizeRung::Column, true, false, false},
        {90, 60, SizeRung::Cell, true, false, false},
        {150, 62, SizeRung::Wide, true, false, true},
        {120, 150, SizeRung::Full, true, true, true},
    };
    for (const auto& r : rows) {
        INFO(r.w << "x" << r.h);
        const auto v = map_verdict(r.w, r.h);
        CHECK(v.rung == r.rung);
        CHECK(v.fits);
        CHECK_FALSE(v.placeholder);
        CHECK(v.show_map == r.map);
        CHECK(v.show_skip_button == r.skip);
        CHECK(v.show_name == r.name);
    }
}

TEST_CASE("exclude_map layout: a placeholder never draws the map, a name or a button",
          "[exclude_map][layout]") {
    for (const auto state : {ExcludeMapState::NoObjects, ExcludeMapState::SingleObject}) {
        for (const auto [w, h] : {std::pair{16, 16}, std::pair{60, 34}, std::pair{234, 232}}) {
            INFO(w << "x" << h);
            const auto v = placeholder_verdict(w, h, state);
            CHECK(v.placeholder);
            CHECK(v.fits);
            CHECK_FALSE(v.show_map);
            CHECK_FALSE(v.show_skip_button);
            CHECK_FALSE(v.show_name);
            CHECK((v.rung == SizeRung::Glance || v.rung == SizeRung::Strip));
        }
    }
}

TEST_CASE("exclude_map layout: the placeholder caption needs its own box",
          "[exclude_map][layout]") {
    CHECK(placeholder_verdict(16, 16, ExcludeMapState::NoObjects).rung == SizeRung::Glance);
    CHECK(placeholder_verdict(60, 34, ExcludeMapState::NoObjects).rung == SizeRung::Strip);
    CHECK(placeholder_verdict(59, 34, ExcludeMapState::NoObjects).rung == SizeRung::Glance);
    CHECK(placeholder_verdict(60, 33, ExcludeMapState::NoObjects).rung == SizeRung::Glance);
    CHECK_FALSE(placeholder_verdict(15, 16, ExcludeMapState::NoObjects).fits);
}

TEST_CASE("exclude_map layout: fits is monotonic across a sweep", "[exclude_map][layout]") {
    for (const auto state : {ExcludeMapState::Objects, ExcludeMapState::NoObjects}) {
        const RungNeeds& needs = state == ExcludeMapState::Objects ? kObjects : kPlaceholder;
        bool fitted = false;
        for (int w = 0; w <= 260; ++w) {
            const bool now = decide_exclude_map_layout({w, 120, state, needs, 0}).fits;
            if (fitted)
                CHECK(now);
            fitted = fitted || now;
        }
        fitted = false;
        for (int h = 0; h <= 260; ++h) {
            const bool now = decide_exclude_map_layout({120, h, state, needs, 0}).fits;
            if (fitted)
                CHECK(now);
            fitted = fitted || now;
        }
        CHECK_FALSE(decide_exclude_map_layout({0, 0, state, needs, 0}).fits);
    }
}

TEST_CASE("exclude_map layout: classify_exclude_map is a decision table", "[exclude_map][layout]") {
    struct Row {
        bool printing;
        int defined;
        ExcludeMapState state;
    };
    const Row rows[] = {
        {false, 0, ExcludeMapState::NoObjects}, {false, 1, ExcludeMapState::NoObjects},
        {false, 6, ExcludeMapState::NoObjects}, {true, 0, ExcludeMapState::NoObjects},
        {true, -1, ExcludeMapState::NoObjects}, {true, 1, ExcludeMapState::SingleObject},
        {true, 2, ExcludeMapState::Objects},    {true, 6, ExcludeMapState::Objects},
    };
    for (const auto& r : rows) {
        INFO("printing=" << r.printing << " defined=" << r.defined);
        CHECK(classify_exclude_map(r.printing, r.defined) == r.state);
    }
}

TEST_CASE("exclude_map layout: remaining is defined minus excluded", "[exclude_map][layout]") {
    const std::vector<std::string> defined = {"a", "b", "c", "d", "e", "f"};

    auto counts = count_objects(defined, {});
    CHECK(counts.total == 6);
    CHECK(counts.skipped == 0);
    CHECK(counts.remaining() == 6);

    counts = count_objects(defined, {"b", "e"});
    CHECK(counts.skipped == 2);
    CHECK(counts.remaining() == 4);

    // A name the print does not define is not a skipped object.
    counts = count_objects(defined, {"b", "gone"});
    CHECK(counts.skipped == 1);
    CHECK(counts.remaining() == 5);

    counts = count_objects(defined, {"a", "b", "c", "d", "e", "f"});
    CHECK(counts.remaining() == 0);

    counts = count_objects({}, {"a"});
    CHECK(counts.total == 0);
    CHECK(counts.skipped == 0);
}
