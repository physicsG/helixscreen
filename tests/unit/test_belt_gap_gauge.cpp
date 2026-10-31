// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/// The belt gap gauge: pure mapping and severity rules, plus the widget's
/// set() contract (no children, safe before layout).

#include "ui_belt_gap_gauge.h"

#include "../lvgl_test_fixture.h"
#include "belt_tune_session.h"
#include "helix-xml/src/xml/lv_xml.h"
#include "theme_manager.h"

#include <cstdint>

#include "../catch_amalgamated.hpp"

using namespace helix::ui;
using helix::calibration::belt_tune::kGaugeWarnHz;

TEST_CASE("belt gap gauge x mapping", "[belt][gauge]") {
    constexpr int kTrack = 201;
    constexpr float kRange = 10.0f;

    SECTION("zero is centred") {
        CHECK(belt_gap_gauge_x(0.0f, kRange, kTrack) == 100);
    }
    SECTION("plus and minus range reach the ends") {
        CHECK(belt_gap_gauge_x(kRange, kRange, kTrack) == kTrack - 1);
        CHECK(belt_gap_gauge_x(-kRange, kRange, kTrack) == 0);
    }
    SECTION("beyond the range clamps to the track") {
        CHECK(belt_gap_gauge_x(50.0f, kRange, kTrack) == kTrack - 1);
        CHECK(belt_gap_gauge_x(-50.0f, kRange, kTrack) == 0);
    }
    SECTION("half range sits half way to the end") {
        CHECK(belt_gap_gauge_x(5.0f, kRange, kTrack) == 150);
        CHECK(belt_gap_gauge_x(-5.0f, kRange, kTrack) == 50);
    }
    SECTION("degenerate inputs stay inside the track") {
        CHECK(belt_gap_gauge_x(3.0f, 0.0f, kTrack) == 100);
        CHECK(belt_gap_gauge_x(3.0f, kRange, 0) == 0);
    }
}

TEST_CASE("belt gap severity bands", "[belt][gauge]") {
    constexpr float kTol = 1.0f;
    CHECK(belt_gap_severity(0.0f, kTol) == GapSeverity::MATCHED);
    CHECK(belt_gap_severity(kTol, kTol) == GapSeverity::MATCHED);
    CHECK(belt_gap_severity(-kTol, kTol) == GapSeverity::MATCHED);
    CHECK(belt_gap_severity(kTol + 0.01f, kTol) == GapSeverity::NEAR);
    CHECK(belt_gap_severity(kGaugeWarnHz, kTol) == GapSeverity::NEAR);
    CHECK(belt_gap_severity(-kGaugeWarnHz, kTol) == GapSeverity::NEAR);
    CHECK(belt_gap_severity(kGaugeWarnHz + 0.01f, kTol) == GapSeverity::FAR);
    CHECK(belt_gap_severity(-8.0f, kTol) == GapSeverity::FAR);
    // A tolerance wider than the warning band still reads matched inside it.
    CHECK(belt_gap_severity(3.5f, 4.0f) == GapSeverity::MATCHED);
}

TEST_CASE_METHOD(LVGLTestFixture, "belt gap gauge set() is child-free and layout-safe",
                 "[belt][gauge]") {
    register_belt_gap_gauge_widget();
    lv_obj_t* gauge =
        static_cast<lv_obj_t*>(lv_xml_create(test_screen(), "belt_gap_gauge", nullptr));
    REQUIRE(gauge != nullptr);
    CHECK(lv_obj_get_child_count(gauge) == 0);

    ui_belt_gap_gauge_set(gauge, 2.5f, std::nullopt, 1.0f, 10.0f);
    CHECK(lv_obj_get_child_count(gauge) == 0);
    ui_belt_gap_gauge_set(gauge, -2.5f, 4.0f, 1.0f, 10.0f);
    CHECK(lv_obj_get_child_count(gauge) == 0);

    // Before layout the gauge has zero size; drawing must be a no-op.
    lv_obj_set_size(gauge, 0, 0);
    ui_belt_gap_gauge_set(gauge, 99.0f, -99.0f, 1.0f, 10.0f);
    lv_obj_invalidate(gauge);
    lv_refr_now(nullptr);

    lv_obj_set_size(gauge, 200, 40);
    lv_refr_now(nullptr);
    ui_belt_gap_gauge_set(nullptr, 1.0f, std::nullopt, 1.0f, 10.0f);
    CHECK(lv_obj_get_child_count(gauge) == 0);
}

namespace {

struct Px {
    uint8_t r = 0, g = 0, b = 0, a = 0;
    bool operator==(const Px& o) const {
        return r == o.r && g == o.g && b == o.b && a == o.a;
    }
};

/// The gauge's own draw, snapshotted: pixels it never touches stay alpha 0.
class GaugeShot {
  public:
    explicit GaugeShot(lv_obj_t* gauge) : snap_(lv_snapshot_take(gauge, LV_COLOR_FORMAT_ARGB8888)) {
        REQUIRE(snap_ != nullptr);
    }
    ~GaugeShot() {
        lv_draw_buf_destroy(snap_);
    }
    Px at(int x, int y) const {
        const uint32_t stride = snap_->header.stride;
        const uint8_t* p = snap_->data + static_cast<uint32_t>(y) * stride + x * 4;
        return Px{p[2], p[1], p[0], p[3]};
    }
    bool untouched(int x, int y) const {
        return at(x, y).a == 0;
    }
    int drawn() const {
        int n = 0;
        for (uint32_t y = 0; y < snap_->header.h; ++y) {
            for (uint32_t x = 0; x < snap_->header.w; ++x) {
                n += at(static_cast<int>(x), static_cast<int>(y)).a != 0 ? 1 : 0;
            }
        }
        return n;
    }

  private:
    lv_draw_buf_t* snap_;
};

Px color_px(const char* token) {
    const lv_color_t c = theme_manager_get_color(token);
    return Px{c.red, c.green, c.blue, 255};
}

} // namespace

TEST_CASE_METHOD(LVGLTestFixture,
                 "belt gap gauge draws the track, band and needles where the gap puts them",
                 "[belt][gauge][render]") {
    register_belt_gap_gauge_widget();
    lv_obj_t* gauge =
        static_cast<lv_obj_t*>(lv_xml_create(test_screen(), "belt_gap_gauge", nullptr));
    REQUIRE(gauge != nullptr);

    // 200x40: needles are 4 wide and 30 tall, the track 10 tall, centred at
    // y=20 and running x=4..195. Range 10 Hz puts a 5 Hz gap 75% along, at
    // x=147; -5 Hz at x=52; the 1 Hz tolerance band spans x=90..109.
    lv_obj_set_size(gauge, 200, 40);
    lv_obj_update_layout(gauge);

    SECTION("track, band and filled needle") {
        ui_belt_gap_gauge_set(gauge, 5.0f, std::nullopt, 1.0f, 10.0f);
        const GaugeShot shot(gauge);

        CHECK_FALSE(shot.untouched(30, 20));
        CHECK(shot.untouched(2, 20));
        CHECK(shot.untouched(197, 20));
        CHECK_FALSE(shot.untouched(30, 15));
        CHECK_FALSE(shot.untouched(30, 25));
        CHECK(shot.untouched(30, 13));
        CHECK(shot.untouched(30, 27));

        // The band sits on the track, centred.
        CHECK_FALSE(shot.at(89, 20) == shot.at(90, 20));
        CHECK_FALSE(shot.at(109, 20) == shot.at(110, 20));
        CHECK(shot.at(95, 20) == shot.at(104, 20));
        CHECK(shot.at(90, 15) == shot.at(90, 25));
        CHECK(shot.untouched(95, 13));

        // A 5 Hz gap is past the warning band: opaque danger, 30 tall.
        CHECK(shot.at(147, 6) == color_px("danger"));
        CHECK(shot.at(147, 34) == color_px("danger"));
        CHECK(shot.at(145, 20) == color_px("danger"));
        CHECK(shot.at(149, 20) == color_px("danger"));
        CHECK(shot.untouched(147, 3));
        CHECK(shot.untouched(147, 37));
        CHECK_FALSE(shot.at(143, 20).a == 255);
        CHECK_FALSE(shot.at(151, 20).a == 255);
        // No previous round: no hollow needle and no arrow.
        CHECK_FALSE(shot.at(52, 5).a == 255);
        CHECK_FALSE(shot.at(70, 20).a == 255);
    }

    SECTION("the needle takes its colour from the gap") {
        ui_belt_gap_gauge_set(gauge, 2.0f, std::nullopt, 1.0f, 10.0f);
        CHECK(GaugeShot(gauge).at(119, 6) == color_px("warning"));
        ui_belt_gap_gauge_set(gauge, 0.5f, std::nullopt, 1.0f, 10.0f);
        CHECK(GaugeShot(gauge).at(104, 6) == color_px("success"));
    }

    SECTION("the previous round is a hollow needle with an arrow to this round") {
        ui_belt_gap_gauge_set(gauge, 5.0f, -5.0f, 1.0f, 10.0f);
        const GaugeShot shot(gauge);

        // Hollow: an opaque muted border around the needle's box.
        CHECK(shot.at(50, 20) == color_px("text_muted"));
        CHECK(shot.at(54, 20) == color_px("text_muted"));
        CHECK(shot.at(52, 5) == color_px("text_muted"));
        CHECK(shot.at(52, 35) == color_px("text_muted"));
        CHECK(shot.untouched(52, 3));
        CHECK(shot.at(147, 6) == color_px("danger"));

        // The arrow runs between the needles, tip at x=143 on this round's.
        CHECK(shot.at(70, 20).a >= 200);
        CHECK(shot.at(130, 20).a >= 200);
        CHECK_FALSE(shot.at(55, 20).a >= 200);
        CHECK(shot.at(138, 20).a >= 200);
        CHECK(shot.at(138, 21).a >= 200);
        CHECK_FALSE(shot.at(138, 27).a >= 200);
        CHECK_FALSE(shot.at(144, 20).a >= 200);
        CHECK(shot.at(136, 18).a >= 200);
        CHECK_FALSE(shot.at(142, 16).a >= 200);
    }

    SECTION("the arrow points back when the gap shrank") {
        ui_belt_gap_gauge_set(gauge, -5.0f, 5.0f, 1.0f, 10.0f);
        const GaugeShot shot(gauge);

        // This round's needle is at x=52 now; the arrow's tip is at x=56.
        CHECK(shot.at(52, 6) == color_px("danger"));
        CHECK(shot.at(60, 20).a >= 200);
        CHECK(shot.at(100, 20).a >= 200);
        CHECK(shot.at(140, 20).a >= 200);
        CHECK(shot.at(61, 22).a >= 200);
        CHECK_FALSE(shot.at(61, 28).a >= 200);
        CHECK_FALSE(shot.at(57, 17).a >= 200);
        CHECK(shot.at(63, 17).a >= 200);
        // The shaft stops at the head's base (x=64), not beyond the tip.
        CHECK_FALSE(shot.at(55, 20).a >= 200);
    }

    SECTION("needles closer than an arrowhead get no arrow") {
        // 3.72 Hz puts the previous needle at x=135, exactly 12 from x=147:
        // the head (8) plus a needle width (4) leaves no room.
        ui_belt_gap_gauge_set(gauge, 5.0f, 3.72f, 1.0f, 10.0f);
        const GaugeShot shot(gauge);
        CHECK(shot.at(135, 6) == color_px("text_muted"));
        CHECK_FALSE(shot.at(141, 20).a >= 200);
        CHECK_FALSE(shot.at(140, 20).a >= 200);
    }
}

TEST_CASE_METHOD(LVGLTestFixture, "belt gap gauge draws nothing below its minimum size",
                 "[belt][gauge][render]") {
    register_belt_gap_gauge_widget();
    lv_obj_t* gauge =
        static_cast<lv_obj_t*>(lv_xml_create(test_screen(), "belt_gap_gauge", nullptr));
    REQUIRE(gauge != nullptr);
    ui_belt_gap_gauge_set(gauge, 2.0f, -2.0f, 1.0f, 10.0f);

    const auto drawn_at = [&](int w, int h) {
        lv_obj_set_size(gauge, w, h);
        lv_obj_update_layout(gauge);
        return GaugeShot(gauge).drawn();
    };
    CHECK(drawn_at(20, 40) > 0);
    CHECK(drawn_at(200, 8) > 0);
    CHECK(drawn_at(19, 40) == 0);
    CHECK(drawn_at(200, 7) == 0);
    CHECK(drawn_at(10, 40) == 0);
    CHECK(drawn_at(200, 4) == 0);
}
