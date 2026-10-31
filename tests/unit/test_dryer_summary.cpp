// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_dryer_summary.cpp
 * @brief summarize_dryers() over box zones, the chamber cycle and bed drying: which
 * sources exist, which are drying, progress, highest humidity and the primary source.
 *
 * Run with: ./build/bin/helix-tests "[dryer][summary]"
 */

#include "dryer_summary.h"

#include "../catch_amalgamated.hpp"

using namespace helix;
using helix::printer::EnvironmentZone;
using helix::printer::ZoneDryingState;

namespace {

EnvironmentZone box(int unit, const char* label, bool dryer, bool active = false, int remaining = 0,
                    int duration = 0, float rh = -1.0f) {
    EnvironmentZone z;
    z.id = std::string("unit-") + std::to_string(unit);
    z.label = label;
    z.unit_index = unit;
    z.dryer.supported = dryer;
    z.dryer.active = active;
    z.dryer.remaining_min = remaining;
    z.dryer.duration_min = duration;
    z.dryer.target_temp_c = 55.0f;
    z.state = active ? ZoneDryingState::Active : ZoneDryingState::Idle;
    if (rh >= 0.0f) {
        z.env.has_humidity = true;
        z.env.humidity_pct = rh;
        z.env.temperature_c = 24.0f;
    }
    return z;
}

} // namespace

TEST_CASE("dryer summary: nothing to dry with is unavailable", "[dryer][summary]") {
    const DryerSummary s = summarize_dryers({});
    CHECK_FALSE(s.available);
    CHECK(s.sources.empty());
    CHECK(s.drying_count == 0);
    CHECK(s.highest_rh_pct == -1);
}

TEST_CASE("dryer summary: availability is any source", "[dryer][summary]") {
    DryerSummaryInput box_only;
    box_only.zones = {box(0, "Box 1", true)};
    CHECK(summarize_dryers(box_only).available);

    DryerSummaryInput passive_only;
    passive_only.zones = {box(0, "Box 1", false, false, 0, 0, 40.0f)};
    CHECK_FALSE(summarize_dryers(passive_only).available);

    DryerSummaryInput chamber;
    chamber.chamber.available = true;
    CHECK(summarize_dryers(chamber).available);

    DryerSummaryInput bed;
    bed.bed.available = true;
    CHECK(summarize_dryers(bed).available);
}

TEST_CASE("dryer summary: sources are ordered boxes, chamber, bed", "[dryer][summary]") {
    DryerSummaryInput in;
    in.bed.available = true;
    in.chamber.available = true;
    in.zones = {box(0, "A", true), box(1, "B", true)};
    const DryerSummary s = summarize_dryers(in);
    REQUIRE(s.sources.size() == 4);
    CHECK(s.sources[0].name == "A");
    CHECK(s.sources[1].name == "B");
    CHECK(s.sources[2].kind == DryerSourceKind::Chamber);
    CHECK(s.sources[3].kind == DryerSourceKind::Bed);
}

TEST_CASE("dryer summary: progress is 1 - remaining / duration, -1 when unknown",
          "[dryer][summary]") {
    CHECK(progress_pct_from(60, 240) == 75);
    CHECK(progress_pct_from(240, 240) == 0);
    CHECK(progress_pct_from(0, 240) == 100);
    CHECK(progress_pct_from(300, 240) == 0);
    CHECK(progress_pct_from(-1, 240) == -1);
    CHECK(progress_pct_from(60, 0) == -1);

    DryerSummaryInput in;
    in.zones = {box(0, "A", true, true, 60, 240), box(1, "B", true, true, 60, 0)};
    const DryerSummary s = summarize_dryers(in);
    CHECK(s.sources[0].progress_pct == 75);
    CHECK(s.sources[1].progress_pct == -1);
    CHECK(s.sources[1].drying);
}

TEST_CASE("dryer summary: an idle source carries no cycle numbers", "[dryer][summary]") {
    DryerSummaryInput in;
    in.zones = {box(0, "A", true, false, 90, 240)};
    const DryerSummary s = summarize_dryers(in);
    CHECK_FALSE(s.sources[0].drying);
    CHECK(s.sources[0].remaining_min == -1);
    CHECK(s.sources[0].progress_pct == -1);
    CHECK(s.sources[0].target_c == -1);
}

TEST_CASE("dryer summary: highest RH counts only zones with a dryer", "[dryer][summary]") {
    DryerSummaryInput in;
    in.zones = {box(0, "A", true, false, 0, 0, 31.4f), box(1, "B", false, false, 0, 0, 52.6f),
                box(2, "C", true, false, 0, 0, 44.6f), box(3, "D", true)};
    const DryerSummary s = summarize_dryers(in);
    CHECK(s.highest_rh_pct == 45);
    CHECK(s.sources.size() == 3);
    CHECK(s.sources[0].humidity_pct == 31);
    CHECK(s.sources[2].humidity_pct == -1);
}

TEST_CASE("dryer summary: a passive box's humidity never reaches a chamber-only tile",
          "[dryer][summary]") {
    DryerSummaryInput in;
    in.zones = {box(0, "CFS", false, false, 0, 0, 19.0f)};
    in.chamber.available = true;
    in.chamber.current_c = 30;
    const DryerSummary s = summarize_dryers(in);
    CHECK(s.available);
    CHECK(s.highest_rh_pct == -1);
    REQUIRE(s.sources.size() == 1);
    CHECK(s.sources[0].kind == DryerSourceKind::Chamber);
    CHECK(s.sources[0].humidity_pct == -1);
}

TEST_CASE("dryer summary: zones over one unit dryer make one row", "[dryer][summary]") {
    DryerSummaryInput in;
    in.zones = {box(0, "", true, true, 30, 60), box(0, "", true, true, 30, 60), box(1, "", true)};
    const DryerSummary s = summarize_dryers(in);
    CHECK(s.sources.size() == 2);
    CHECK(s.drying_count == 1);
}

TEST_CASE("dryer summary: chamber and bed rows read their own cycle", "[dryer][summary]") {
    DryerSummaryInput in;
    in.chamber = {true, true, 30, 120, 41, 55};
    in.bed = {true, true, 100, 60, 70, 70};
    const DryerSummary s = summarize_dryers(in);
    REQUIRE(s.sources.size() == 2);
    CHECK(s.drying_count == 2);
    CHECK(s.sources[0].progress_pct == 75);
    CHECK(s.sources[0].remaining_min == 30);
    CHECK(s.sources[0].target_c == 55);
    CHECK(s.sources[1].progress_pct == 60);
    CHECK(s.sources[1].remaining_min == 100);

    in.chamber.active = false;
    in.bed.running = false;
    const DryerSummary idle = summarize_dryers(in);
    CHECK(idle.drying_count == 0);
    CHECK(idle.sources[0].progress_pct == -1);
    CHECK(idle.sources[1].remaining_min == -1);
}

TEST_CASE("dryer summary: the primary source ends first", "[dryer][summary]") {
    DryerSummaryInput in;
    in.zones = {box(0, "A", true, true, 120, 240), box(1, "B", true, true, 20, 240),
                box(2, "C", true, true, -1, 240), box(3, "D", true)};
    const DryerSummary s = summarize_dryers(in);
    REQUIRE(primary_source(s) != nullptr);
    CHECK(primary_source(s)->name == "B");

    DryerSummaryInput none;
    none.zones = {box(0, "A", true)};
    const DryerSummary idle = summarize_dryers(none);
    CHECK(primary_source(idle) == nullptr);
}

TEST_CASE("dryer summary: a row opens its own source", "[dryer][summary][tap]") {
    DryerSource box_src;
    box_src.unit_index = 3;
    CHECK(tap_for_source(box_src).target == DryerTapTarget::Environment);
    CHECK(tap_for_source(box_src).unit_index == 3);
    DryerSource chamber_src;
    chamber_src.kind = DryerSourceKind::Chamber;
    CHECK(tap_for_source(chamber_src).target == DryerTapTarget::ChamberModal);
    DryerSource bed_src;
    bed_src.kind = DryerSourceKind::Bed;
    CHECK(tap_for_source(bed_src).target == DryerTapTarget::BedModal);
}

TEST_CASE("dryer summary: the tile opens the environment overlay when any box has a dryer",
          "[dryer][summary][tap]") {
    DryerSummaryInput in;
    in.chamber = {true, true, 30, 60, 40, 55};
    in.bed.available = true;
    in.zones = {box(2, "A", true), box(5, "B", true, true, 20, 60)};
    const DryerTap drying = tap_for_tile(summarize_dryers(in));
    // B ends first (20 min against the chamber's 30) and is a box: its unit opens.
    CHECK(drying.target == DryerTapTarget::Environment);
    CHECK(drying.unit_index == 5);

    in.zones = {box(2, "A", true), box(5, "B", true)};
    in.chamber.active = false;
    const DryerTap idle = tap_for_tile(summarize_dryers(in));
    CHECK(idle.target == DryerTapTarget::Environment);
    CHECK(idle.unit_index == 2);
}

TEST_CASE("dryer summary: with no box the tile opens the chamber, then the bed",
          "[dryer][summary][tap]") {
    DryerSummaryInput in;
    in.bed.available = true;
    CHECK(tap_for_tile(summarize_dryers(in)).target == DryerTapTarget::BedModal);
    in.chamber.available = true;
    CHECK(tap_for_tile(summarize_dryers(in)).target == DryerTapTarget::ChamberModal);
    CHECK(tap_for_tile(summarize_dryers({})).target == DryerTapTarget::None);
}

TEST_CASE("chamber cycle: a cycle seen beginning keeps the longest remaining time",
          "[dryer][summary][chamber]") {
    ChamberCycle c = next_chamber_cycle({}, true, false, -1);
    c = next_chamber_cycle(c, true, true, 120);
    CHECK(c.counted);
    CHECK(c.total_min == 120);
    c = next_chamber_cycle(c, true, true, 90);
    CHECK(c.total_min == 120);
    c = next_chamber_cycle(c, true, true, 130); // the device extended it
    CHECK(c.total_min == 130);
    CHECK(progress_pct_from(65, c.total_min) == 50);
}

TEST_CASE("chamber cycle: a cycle already running at the first reading has no total",
          "[dryer][summary][chamber]") {
    ChamberCycle c = next_chamber_cycle({}, true, true, 80);
    CHECK_FALSE(c.counted);
    CHECK(c.total_min == -1);
    c = next_chamber_cycle(c, true, true, 70);
    CHECK_FALSE(c.counted);
    CHECK(c.total_min == -1);
}

TEST_CASE("chamber cycle: the end resets, and a shorter second cycle starts from its own total",
          "[dryer][summary][chamber]") {
    ChamberCycle c = next_chamber_cycle({}, true, false, -1);
    c = next_chamber_cycle(c, true, true, 240);
    c = next_chamber_cycle(c, true, true, 100);
    c = next_chamber_cycle(c, true, false, -1);
    CHECK_FALSE(c.counted);
    CHECK(c.total_min == -1);
    c = next_chamber_cycle(c, true, true, 60);
    CHECK(c.counted);
    CHECK(c.total_min == 60);
}

TEST_CASE(
    "chamber cycle: before a real reading nothing is learned, then the first one is the baseline",
    "[dryer][summary][chamber]") {
    ChamberCycle c = next_chamber_cycle({}, false, false, -1);
    CHECK_FALSE(c.seen);
    // The default idle never counted as a baseline, so a cycle the first real frame
    // reports as running was not seen starting.
    c = next_chamber_cycle(c, true, true, 80);
    CHECK_FALSE(c.counted);
    CHECK(c.total_min == -1);

    ChamberCycle idle = next_chamber_cycle({}, false, false, -1);
    idle = next_chamber_cycle(idle, true, false, -1);
    idle = next_chamber_cycle(idle, true, true, 60);
    CHECK(idle.counted);
}

TEST_CASE("chamber cycle: a remaining time reported late still sets the total",
          "[dryer][summary][chamber]") {
    ChamberCycle c = next_chamber_cycle({}, true, false, -1);
    c = next_chamber_cycle(c, true, true, -1);
    CHECK(c.counted);
    CHECK(c.total_min == -1);
    c = next_chamber_cycle(c, true, true, 100);
    CHECK(c.total_min == 100);
}

TEST_CASE("dryer summary: a chamber cycle with nothing left reads 0 and 100 percent",
          "[dryer][summary][chamber]") {
    DryerSummaryInput in;
    in.chamber = {true, true, 0, 120, 50, 55};
    const DryerSummary s = summarize_dryers(in);
    CHECK(s.sources[0].remaining_min == 0);
    CHECK(s.sources[0].progress_pct == 100);
}
