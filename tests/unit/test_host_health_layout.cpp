// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_host_health_layout.cpp
 * @brief decide_host_health_layout() at each rung boundary, idle and printing, and the
 * texts the Glance rung and the readouts compose.
 *
 * The needs below are boxes of the shape HostHealthWidget measures at the medium tier;
 * the budgets are the content boxes of the design's pixel-budget table (the widget's
 * chrome at medium is 9px on each axis). The widget tests pin the real measured values.
 *
 * Run with: ./build/bin/helix-tests "[host_health][layout]"
 */

#include "src/ui/panel_widgets/host_health_layout.h"

#include "../catch_amalgamated.hpp"

using namespace helix;
using status::HealthCause;

namespace {

constexpr RungNeeds kNeeds{rung_box(33, 31), rung_box(88, 19),  rung_box(30, 79),
                           rung_box(90, 49), rung_box(180, 41), rung_box(180, 140)};
constexpr int kPitch = 20;
constexpr int kChrome = 9;

HostHealthVerdict verdict_for(int w, int h, bool printing = false, int mcus = 2,
                              int full_min_h = 0) {
    return decide_host_health_layout({w, h, printing, mcus, kPitch, kNeeds, full_min_h});
}

HostHealthVerdict verdict_for_tile(int tile_w, int tile_h, bool printing = false) {
    return verdict_for(tile_w - kChrome, tile_h - kChrome, printing);
}

} // namespace

TEST_CASE("host_health layout: rung boundaries are exact", "[host_health][layout]") {
    CHECK(verdict_for(33, 31).rung == SizeRung::Glance);
    CHECK(verdict_for(88, 19).rung == SizeRung::Strip);
    CHECK(verdict_for(87, 30).rung == SizeRung::Glance);
    CHECK(verdict_for(88, 18).rung == SizeRung::Glance);
    CHECK(verdict_for(30, 79).rung == SizeRung::Column);
    CHECK(verdict_for(30, 78).rung == SizeRung::Glance);
    CHECK(verdict_for(90, 49).rung == SizeRung::Cell);
    CHECK(verdict_for(89, 49).rung == SizeRung::Strip);
    CHECK(verdict_for(90, 48).rung == SizeRung::Strip);
    CHECK(verdict_for(180, 41).rung == SizeRung::Wide);
    CHECK(verdict_for(179, 41).rung == SizeRung::Strip);
    CHECK(verdict_for(180, 40).rung == SizeRung::Strip);
    CHECK(verdict_for(179, 49).rung == SizeRung::Cell);
    CHECK(verdict_for(180, 140).rung == SizeRung::Full);
    CHECK(verdict_for(179, 140).rung == SizeRung::Cell);
    CHECK(verdict_for(180, 139).rung == SizeRung::Wide);
}

TEST_CASE("host_health layout: the design's pixel budgets", "[host_health][layout]") {
    struct Row {
        const char* name;
        int w, h;
        SizeRung rung;
    };
    const Row rows[] = {
        {"medium 0.5x0.5", 55, 54, SizeRung::Glance}, {"medium 1x0.5", 114, 54, SizeRung::Strip},
        {"medium 0.5x1", 55, 113, SizeRung::Column},  {"medium 1x1", 114, 113, SizeRung::Cell},
        {"large 0.5x0.5", 51, 53, SizeRung::Glance},  {"large 0.5x1", 51, 112, SizeRung::Column},
        {"large 1x0.5", 108, 53, SizeRung::Strip},
    };
    for (const auto& r : rows) {
        for (const bool printing : {false, true}) {
            INFO(r.name << " printing=" << printing);
            const auto v = verdict_for_tile(r.w, r.h, printing);
            CHECK(v.fits);
            CHECK(v.rung == r.rung);
        }
    }
}

TEST_CASE("host_health layout: the MCU row replaces memory only at Wide while printing",
          "[host_health][layout]") {
    CHECK(verdict_for(180, 41, true).show_mcu_row);
    CHECK_FALSE(verdict_for(180, 41, false).show_mcu_row);
    // A printer that reports no MCU keeps the memory row.
    CHECK_FALSE(verdict_for(180, 41, true, 0).show_mcu_row);
    // Other rungs never show it.
    CHECK_FALSE(verdict_for(90, 49, true).show_mcu_row);
    CHECK_FALSE(verdict_for(180, 140, true).show_mcu_row);
}

TEST_CASE("host_health layout: Full holds as many MCU rows as fit, never more than exist",
          "[host_health][layout]") {
    // The Full box already holds the first row; each further pitch of height adds one.
    CHECK(verdict_for(180, 140, false, 4).mcu_rows == 1);
    CHECK(verdict_for(180, 159, false, 4).mcu_rows == 1);
    CHECK(verdict_for(180, 160, false, 4).mcu_rows == 2);
    CHECK(verdict_for(180, 199, false, 4).mcu_rows == 3);
    CHECK(verdict_for(180, 200, false, 4).mcu_rows == 4);
    CHECK(verdict_for(180, 400, false, 4).mcu_rows == 4);
    CHECK(verdict_for(180, 400, false, 1).mcu_rows == 1);
    CHECK(verdict_for(180, 400, false, 0).mcu_rows == 0);
    // Only Full draws rows.
    CHECK(verdict_for(180, 139, false, 4).mcu_rows == 0);
}

TEST_CASE("host_health layout: Full also needs the tall size class", "[host_health][layout]") {
    // A box that holds the host block but is no taller than a 2x1 stays Wide.
    CHECK(verdict_for(180, 150, false, 2, 0).rung == SizeRung::Full);
    CHECK(verdict_for(180, 150, false, 2, 162).rung == SizeRung::Wide);
    CHECK(verdict_for(180, 161, false, 2, 162).rung == SizeRung::Wide);
    CHECK(verdict_for(180, 162, false, 2, 162).rung == SizeRung::Full);
    // The floor does not change how many MCU rows the measured content holds.
    CHECK(verdict_for(180, 200, false, 4, 162).mcu_rows == 4);
    CHECK(verdict_for(180, 162, false, 4, 162).mcu_rows == 2);
    // And never lifts fits.
    CHECK(verdict_for(33, 31, false, 2, 162).fits);
}

TEST_CASE("host_health layout: fits is false below Glance and monotonic above",
          "[host_health][layout]") {
    CHECK_FALSE(verdict_for(32, 31).fits);
    CHECK_FALSE(verdict_for(33, 30).fits);
    CHECK(verdict_for(33, 31).fits);

    bool fitted_w = false;
    for (int w = 0; w <= 300; ++w) {
        const bool now = verdict_for(w, 200).fits;
        if (fitted_w)
            CHECK(now);
        fitted_w = fitted_w || now;
    }
    bool fitted_h = false;
    for (int h = 0; h <= 300; ++h) {
        const bool now = verdict_for(300, h).fits;
        if (fitted_h)
            CHECK(now);
        fitted_h = fitted_h || now;
    }
}

TEST_CASE("host_health layout: the Glance text follows the cause", "[host_health][layout]") {
    const HostHealthReadings r{93, 812, 45, 412};
    CHECK(format_glance_text(HealthCause::None, r) == "93%");
    CHECK(format_glance_text(HealthCause::HostCpu, r) == "93%");
    CHECK(format_glance_text(HealthCause::HostTemp, r) == "81\xc2\xb0");
    CHECK(format_glance_text(HealthCause::McuLoad, r) == "45%");
    CHECK(format_glance_text(HealthCause::DiskFree, r) == "412M");
    CHECK(format_glance_text(HealthCause::Throttle, r) == "!");
    CHECK(format_glance_text(HealthCause::McuRetransmit, r) == "!");

    const HostHealthReadings big{10, 600, 0, 18841};
    CHECK(format_glance_text(HealthCause::DiskFree, big) == "18G");

    // A reading the host does not report draws a dash, not a zero.
    const HostHealthReadings none;
    CHECK(format_glance_text(HealthCause::None, none) == "--");
    CHECK(format_glance_text(HealthCause::HostTemp, none) == "--");
    CHECK(format_glance_text(HealthCause::McuLoad, none) == "--");
    CHECK(format_glance_text(HealthCause::DiskFree, none) == "--");
}

TEST_CASE("host_health layout: a reported zero reads 0%, not a dash", "[host_health][layout]") {
    const HostHealthReadings idle{0, 500, 0, 412};
    CHECK(format_glance_text(HealthCause::HostCpu, idle) == "0%");
    CHECK(format_glance_text(HealthCause::McuLoad, idle) == "0%");
}

TEST_CASE("host_health layout: temperature text rounds to whole degrees or shows a decimal",
          "[host_health][layout]") {
    CHECK(format_cpu_temp(614, 0) == "61\xc2\xb0"
                                     "C");
    CHECK(format_cpu_temp(615, 0) == "62\xc2\xb0"
                                     "C");
    CHECK(format_cpu_temp(614, 1) == "61.4\xc2\xb0"
                                     "C");
    CHECK(format_cpu_temp(-1, 0) == "--");
    CHECK(format_cpu_temp(-1, 1) == "--");
    CHECK(format_percent(42) == "42%");
    CHECK(format_percent(-1) == "--");
}
