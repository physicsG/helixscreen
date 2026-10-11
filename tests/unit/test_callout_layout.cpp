// SPDX-License-Identifier: GPL-3.0-or-later
#include "src/ui/panel_widgets/callout_layout.h"

#include "../catch_amalgamated.hpp"

using namespace helix;

namespace {

// 800x480 2x2 widget: 160x160 container, K1C aspect (1601x1204).
CalloutLayoutInput base() {
    CalloutLayoutInput in;
    in.area_w = 160;
    in.area_h = 140;
    in.image_w = 1601;
    in.image_h = 1204;
    in.tagged = true;
    in.chip_h = 18;
    in.gap = 4;
    in.min_line = 8;
    return in;
}

// TEST_MIRROR_OK: find() looks a chip up in a CalloutLayout; it shares only its name with
// platform_table::find()
const CalloutChipOut* find(const CalloutLayout& l, CalloutKind k) {
    for (const auto& c : l.chips)
        if (c.kind == k)
            return &c;
    return nullptr;
}

bool inside(const CalloutRect& r, int w, int h) {
    return r.x >= 0 && r.y >= 0 && r.x + r.w <= w && r.y + r.h <= h;
}

} // namespace

TEST_CASE("fit_image: contain-fit, centred", "[printer_image][callout_layout]") {
    const auto r = fit_image(200, 100, 400, 400);
    CHECK(r.w == 100);
    CHECK(r.h == 100);
    CHECK(r.x == 50);
    CHECK(r.y == 0);
    CHECK(fit_image(0, 100, 400, 400).w == 0);
    CHECK(fit_image(200, 100, 0, 0).w == 0);
}

TEST_CASE("spread_1d: resolves overlaps and stays within bounds",
          "[printer_image][callout_layout]") {
    std::vector<int> start{10, 12, 90};
    const std::vector<int> size{18, 18, 18};
    spread_1d(start, size, 0, 100, 4);
    CHECK(start[0] == 10);
    CHECK(start[1] == 32);
    CHECK(start[2] == 82); // pulled back up to fit the bottom edge
}

TEST_CASE("single cell shows the image only", "[printer_image][callout_layout]") {
    auto in = base();
    in.single_cell = true;
    in.active = {{CalloutKind::Nozzle, 60, NormPoint{0.5f, 0.3f}}};
    const auto l = compute_callout_layout(in);
    CHECK(l.mode == CalloutMode::ImageOnly);
    CHECK(l.chips.empty());
}

TEST_CASE("image shorter than three chips shows the image only (2x1 cells)",
          "[printer_image][callout_layout]") {
    auto in = base();
    in.area_w = 160;
    in.area_h = 3 * 18 - 1; // fitted K1C image is 53px tall
    in.active = {{CalloutKind::Nozzle, 60, NormPoint{0.5f, 0.3f}}};
    CHECK(compute_callout_layout(in).mode == CalloutMode::ImageOnly);
    in.area_h = 3 * 18;
    CHECK(compute_callout_layout(in).mode != CalloutMode::ImageOnly);
}

TEST_CASE("unlaid-out container shows the image only", "[printer_image][callout_layout]") {
    auto in = base();
    in.area_w = 0;
    in.area_h = 0;
    in.active = {{CalloutKind::Nozzle, 60, NormPoint{0.5f, 0.3f}}};
    CHECK(compute_callout_layout(in).mode == CalloutMode::ImageOnly);
}

TEST_CASE("pinned: chip centred on its point, inside the area", "[printer_image][callout_layout]") {
    auto in = base();
    in.active = {{CalloutKind::Bed, 60, NormPoint{0.46f, 0.57f}}};
    const auto l = compute_callout_layout(in);
    REQUIRE(l.mode == CalloutMode::Pinned);
    const auto* bed = find(l, CalloutKind::Bed);
    REQUIRE(bed);
    const int ax = l.image.x + int(0.46f * l.image.w);
    const int ay = l.image.y + int(0.57f * l.image.h);
    CHECK(bed->rect.x + bed->rect.w / 2 == Catch::Approx(ax).margin(1));
    CHECK(bed->rect.y + bed->rect.h / 2 == Catch::Approx(ay).margin(1));
    CHECK_FALSE(bed->has_line);
    CHECK(inside(bed->rect, in.area_w, in.area_h));
}

TEST_CASE("pinned: a point at the image edge is clamped inside",
          "[printer_image][callout_layout]") {
    auto in = base();
    in.active = {{CalloutKind::Light, 40, NormPoint{0.99f, 0.01f}}};
    const auto l = compute_callout_layout(in);
    CHECK(inside(find(l, CalloutKind::Light)->rect, in.area_w, in.area_h));
}

TEST_CASE("pinned: nozzle and fan merge into the toolhead chip",
          "[printer_image][callout_layout]") {
    auto in = base();
    in.active = {{CalloutKind::Nozzle, 70, NormPoint{0.51f, 0.28f}},
                 {CalloutKind::Fan, 40, NormPoint{0.49f, 0.21f}}};
    in.toolhead = CalloutChipIn{CalloutKind::Toolhead, 110, NormPoint{0.51f, 0.28f}};
    const auto l = compute_callout_layout(in);
    CHECK(l.toolhead_merged);
    CHECK(find(l, CalloutKind::Toolhead));
    CHECK_FALSE(find(l, CalloutKind::Nozzle));
    CHECK_FALSE(find(l, CalloutKind::Fan));
}

TEST_CASE("pinned: nozzle alone does not merge", "[printer_image][callout_layout]") {
    auto in = base();
    in.active = {{CalloutKind::Nozzle, 70, NormPoint{0.51f, 0.28f}}};
    in.toolhead = CalloutChipIn{CalloutKind::Toolhead, 110, NormPoint{0.51f, 0.28f}};
    const auto l = compute_callout_layout(in);
    CHECK_FALSE(l.toolhead_merged);
    CHECK(find(l, CalloutKind::Nozzle));
}

TEST_CASE("tagged image, chip with no point is docked, not at the origin",
          "[printer_image][callout_layout]") {
    auto in = base();
    in.active = {{CalloutKind::Nozzle, 60, NormPoint{0.5f, 0.3f}},
                 {CalloutKind::Chamber, 50, std::nullopt}};
    const auto l = compute_callout_layout(in);
    const auto* ch = find(l, CalloutKind::Chamber);
    REQUIRE(ch);
    CHECK(inside(ch->rect, in.area_w, in.area_h));
    CHECK(ch->rect.y + ch->rect.h == in.area_h - in.gap); // bottom dock row
}

TEST_CASE("untagged image: chips docked along the bottom, no lines, no overlap",
          "[printer_image][callout_layout]") {
    auto in = base();
    in.tagged = false;
    in.active = {{CalloutKind::Nozzle, 60, std::nullopt},
                 {CalloutKind::Bed, 60, std::nullopt},
                 {CalloutKind::Fan, 40, std::nullopt}};
    const auto l = compute_callout_layout(in);
    REQUIRE(l.mode == CalloutMode::Docked);
    REQUIRE(l.chips.size() == 3);
    for (size_t i = 0; i < l.chips.size(); ++i) {
        CHECK_FALSE(l.chips[i].has_line);
        CHECK(inside(l.chips[i].rect, in.area_w, in.area_h));
        for (size_t j = i + 1; j < l.chips.size(); ++j) {
            const auto& a = l.chips[i].rect;
            const auto& b = l.chips[j].rect;
            const bool overlap =
                a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
            CHECK_FALSE(overlap);
        }
    }
}

TEST_CASE("untagged image with a side band: chips stack in the band",
          "[printer_image][callout_layout]") {
    auto in = base();
    in.tagged = false;
    in.area_w = 330;
    in.area_h = 140; // image 186 wide, 72px band each side, chip column needs 68
    in.active = {{CalloutKind::Nozzle, 60, std::nullopt}, {CalloutKind::Bed, 60, std::nullopt}};
    const auto l = compute_callout_layout(in);
    REQUIRE(l.mode == CalloutMode::Docked);
    for (const auto& c : l.chips)
        CHECK(c.rect.x >= l.image.x + l.image.w);
}

TEST_CASE("docked: a chip wider than the area is clamped to fit, not left overhanging",
          "[printer_image][callout_layout]") {
    auto in = base();
    in.tagged = false;
    in.active = {{CalloutKind::Nozzle, 200, std::nullopt}}; // wider than area_w (160)
    const auto l = compute_callout_layout(in);
    REQUIRE(l.mode == CalloutMode::Docked);
    REQUIRE(l.chips.size() == 1);
    CHECK(inside(l.chips[0].rect, in.area_w, in.area_h));
    CHECK(l.chips[0].rect.w <= in.area_w);
}

TEST_CASE("pinned: a merged toolhead chip wider than the area is clamped inside",
          "[printer_image][callout_layout]") {
    auto in = base();
    in.area_w = 136; // narrower than the merged toolhead chip (150)
    in.active = {{CalloutKind::Nozzle, 70, NormPoint{0.51f, 0.28f}},
                 {CalloutKind::Fan, 40, NormPoint{0.49f, 0.21f}}};
    in.toolhead = CalloutChipIn{CalloutKind::Toolhead, 150, NormPoint{0.51f, 0.28f}};
    const auto l = compute_callout_layout(in);
    REQUIRE(l.toolhead_merged);
    const auto* th = find(l, CalloutKind::Toolhead);
    REQUIRE(th);
    CHECK(inside(th->rect, in.area_w, in.area_h));
}

namespace {
// K1C in a 4x2 widget at 800x480: 320x140 container, fitted image 186 wide.
CalloutLayoutInput wide() {
    auto in = base();
    in.area_w = 320;
    in.area_h = 140;
    in.budget = {{CalloutKind::Nozzle, 70, NormPoint{0.51f, 0.28f}},
                 {CalloutKind::Bed, 70, NormPoint{0.46f, 0.57f}},
                 {CalloutKind::Fan, 45, NormPoint{0.49f, 0.21f}},
                 {CalloutKind::Chamber, 70, NormPoint{0.31f, 0.38f}}};
    in.active = in.budget;
    return in;
}
} // namespace

TEST_CASE("both sides when each band fits a column", "[printer_image][callout_layout]") {
    auto in = wide();
    in.area_w = 186 + 2 * (70 + in.gap + in.min_line); // exactly one column per side
    const auto l = compute_callout_layout(in);
    REQUIRE(l.mode == CalloutMode::BothSides);
    CHECK(l.image.x == (in.area_w - l.image.w) / 2); // image stays centred
    for (const auto& c : l.chips) {
        CHECK(c.has_line);
        const bool left = c.rect.x + c.rect.w <= l.image.x;
        const bool right = c.rect.x >= l.image.x + l.image.w;
        CHECK((left || right));
    }
    // Chamber's point is left of centre, so its chip is on the left.
    for (const auto& c : l.chips)
        if (c.kind == CalloutKind::Chamber)
            CHECK(c.rect.x < l.image.x);
}

TEST_CASE("one pixel short of both sides falls to one side", "[printer_image][callout_layout]") {
    auto in = wide();
    in.area_w = 186 + 2 * (70 + in.gap + in.min_line) - 2;
    const auto l = compute_callout_layout(in);
    REQUIRE(l.mode == CalloutMode::OneSide);
    CHECK(l.image.x == 0); // image moved left
    for (const auto& c : l.chips) {
        CHECK(c.has_line);
        CHECK(c.rect.x >= l.image.w);
    }
}

TEST_CASE("one side keeps the image at its full fitted size when it fits",
          "[printer_image][callout_layout]") {
    auto in = wide();
    in.area_w = 186 + (70 + in.gap + in.min_line); // exactly one column beside it
    const auto l = compute_callout_layout(in);
    REQUIRE(l.mode == CalloutMode::OneSide);
    CHECK(l.image.w == 186);
    CHECK(l.image.h == 140);
}

TEST_CASE("a small image shrink unlocks one side instead of pinned",
          "[printer_image][callout_layout]") {
    auto in = wide();
    const int col = 70 + in.gap + in.min_line;
    in.area_w = 186 + col - 14; // the column fits once the image gives up 14px: 7.5%
    const auto l = compute_callout_layout(in);
    REQUIRE(l.mode == CalloutMode::OneSide);
    CHECK(l.image.x == 0);
    CHECK(l.image.w == 186 - 14);
    CHECK(l.image.h == 140 * (186 - 14) / 186); // aspect kept
    CHECK(l.image.y == (in.area_h - l.image.h) / 2);
    for (const auto& c : l.chips) {
        CHECK(c.has_line);
        CHECK(c.rect.x >= l.image.x + l.image.w);
        CHECK(c.rect.x + c.rect.w <= in.area_w);
    }
    // Fit is decided by the budget: a chip going away does not grow the image back.
    in.active = {{CalloutKind::Bed, 70, NormPoint{0.46f, 0.57f}}};
    const auto one = compute_callout_layout(in);
    CHECK(one.mode == CalloutMode::OneSide);
    CHECK(one.image.w == l.image.w);
    CHECK(one.image.x == l.image.x);
}

TEST_CASE("a column needing more than the shrink limit stays pinned",
          "[printer_image][callout_layout]") {
    auto in = wide();
    in.area_w = 186 + (70 + in.gap + in.min_line) - 15; // 15px is over 8% of 186
    const auto l = compute_callout_layout(in);
    CHECK(l.mode == CalloutMode::Pinned);
    CHECK(l.image.w == 186);
}

TEST_CASE("a small image shrink unlocks both sides when one side cannot stack",
          "[printer_image][callout_layout]") {
    auto in = wide();
    in.area_h = 80; // three chips stack, four do not; fitted image 106x80
    const int col = 70 + in.gap + in.min_line;
    in.area_w = 106 + 2 * col - 7;
    const auto l = compute_callout_layout(in);
    REQUIRE(l.mode == CalloutMode::BothSides);
    CHECK(l.image.w == 106 - 7);
    CHECK(l.image.x == (in.area_w - l.image.w) / 2);
    for (const auto& c : l.chips) {
        const bool left = c.rect.x + c.rect.w <= l.image.x;
        const bool right = c.rect.x >= l.image.x + l.image.w;
        CHECK((left || right));
    }
}

TEST_CASE("column that cannot stack its chips falls through to pinned",
          "[printer_image][callout_layout]") {
    auto in = wide();
    for (auto& c : in.budget)
        c.anchor->x = 0.3f; // every point left of centre: one side must hold all four
    in.active = in.budget;
    in.area_w = 400;
    in.area_h = 4 * in.chip_h + 3 * in.gap + 2 * in.gap - 1; // one pixel short for four stacked
    CHECK(compute_callout_layout(in).mode == CalloutMode::Pinned);
    in.area_h += 1;
    CHECK(compute_callout_layout(in).mode != CalloutMode::Pinned);
}

TEST_CASE("mode comes from the budget, so the image does not move when chips change",
          "[printer_image][callout_layout]") {
    auto in = wide();
    in.area_w = 186 + 2 * (70 + in.gap + in.min_line) - 2;
    const auto full = compute_callout_layout(in);
    in.active = {{CalloutKind::Bed, 70, NormPoint{0.46f, 0.57f}}};
    const auto one = compute_callout_layout(in);
    CHECK(one.mode == full.mode);
    CHECK(one.image.x == full.image.x);
    in.active.clear();
    CHECK(compute_callout_layout(in).image.x == full.image.x);
}

TEST_CASE("an empty budget never draws lines", "[printer_image][callout_layout]") {
    auto in = wide();
    in.area_w = 186 + 2 * (70 + in.gap + in.min_line); // room for both sides
    REQUIRE(compute_callout_layout(in).mode == CalloutMode::BothSides);
    in.budget.clear();
    const auto l = compute_callout_layout(in);
    CHECK(l.mode == CalloutMode::Pinned);
    for (const auto& c : l.chips)
        CHECK_FALSE(c.has_line);
}

TEST_CASE("leader line runs from the tagged point to the chip's inner edge",
          "[printer_image][callout_layout]") {
    auto in = wide();
    in.area_w = 186 + 2 * (70 + in.gap + in.min_line);
    in.active = {{CalloutKind::Bed, 70, NormPoint{0.46f, 0.57f}}};
    const auto l = compute_callout_layout(in);
    REQUIRE(l.chips.size() == 1);
    const auto& c = l.chips[0];
    CHECK(c.line_x0 == l.image.x + int(0.46f * l.image.w));
    CHECK(c.line_y0 == l.image.y + int(0.57f * l.image.h));
    const bool at_inner_edge = c.line_x1 == c.rect.x || c.line_x1 == c.rect.x + c.rect.w;
    CHECK(at_inner_edge);
    CHECK(c.line_y1 == c.rect.y + c.rect.h / 2);
}

TEST_CASE("tall widget uses bands above and below", "[printer_image][callout_layout]") {
    auto in = wide();
    in.area_w = 240;
    in.area_h = 480; // image 240x180 centred: 150px above and below
    const auto l = compute_callout_layout(in);
    REQUIRE((l.mode == CalloutMode::BothSides || l.mode == CalloutMode::OneSide));
    for (const auto& c : l.chips) {
        const bool above = c.rect.y + c.rect.h <= l.image.y;
        const bool below = c.rect.y >= l.image.y + l.image.h;
        CHECK((above || below));
    }
}

TEST_CASE("a budget chip with no tagged point falls through to pinned, docked not at the origin",
          "[printer_image][callout_layout]") {
    auto in = wide();
    in.area_w = 186 + 2 * (70 + in.gap + in.min_line); // otherwise fits both sides
    for (auto& c : in.budget)
        if (c.kind == CalloutKind::Chamber)
            c.anchor = std::nullopt;
    for (auto& c : in.active)
        if (c.kind == CalloutKind::Chamber)
            c.anchor = std::nullopt;
    const auto l = compute_callout_layout(in);
    REQUIRE(l.mode == CalloutMode::Pinned);
    const auto* chamber = find(l, CalloutKind::Chamber);
    REQUIRE(chamber);
    CHECK(inside(chamber->rect, in.area_w, in.area_h));
    CHECK(chamber->rect.y + chamber->rect.h == in.area_h - in.gap); // bottom dock row
}

TEST_CASE("an active chip with no tagged point falls through to pinned even when the "
          "budget is fully anchored",
          "[printer_image][callout_layout]") {
    auto in = wide();
    in.area_w = 186 + 2 * (70 + in.gap + in.min_line); // otherwise fits both sides
    for (auto& c : in.active)
        if (c.kind == CalloutKind::Chamber)
            c.anchor = std::nullopt;
    const auto l = compute_callout_layout(in);
    REQUIRE(l.mode == CalloutMode::Pinned);
    const auto* chamber = find(l, CalloutKind::Chamber);
    REQUIRE(chamber);
    CHECK(inside(chamber->rect, in.area_w, in.area_h));
    CHECK(chamber->rect.y + chamber->rect.h == in.area_h - in.gap); // bottom dock row
}

// ---------------------------------------------------------------------------
// Elbow leader lines: 45 degrees toward the chip, then straight into its edge
// ---------------------------------------------------------------------------

using callout_detail::leader_elbow;

TEST_CASE("leader elbow: room for 45 degrees runs 45 degrees, then level into the chip",
          "[printer_image][callout_layout]") {
    const CalloutPoint a{100, 50}, c{200, 80};
    const auto e = leader_elbow(a, c, 8, true);
    CHECK(std::abs(e.x - a.x) == std::abs(e.y - a.y));
    CHECK(e.y == c.y);
    CHECK(e.x == 130);
    // Upward too.
    const auto up = leader_elbow(a, CalloutPoint{200, 20}, 8, true);
    CHECK(up.x == 130);
    CHECK(up.y == 20);
}

TEST_CASE("leader elbow: too steep for 45 degrees keeps a min_line level stub",
          "[printer_image][callout_layout]") {
    const CalloutPoint a{100, 50}, c{120, 100};
    const auto e = leader_elbow(a, c, 8, true);
    CHECK(e.x == c.x - 8);
    CHECK(e.y == c.y);
    // Exactly one pixel short of the 45-degree run plus its stub.
    CHECK(leader_elbow(a, CalloutPoint{120, 63}, 8, true).x == 112);
    CHECK(leader_elbow(a, CalloutPoint{120, 62}, 8, true).x == 112);
    CHECK(leader_elbow(a, CalloutPoint{120, 61}, 8, true).x == 111);
}

TEST_CASE("leader elbow: a chip nearer than min_line never runs the line backwards",
          "[printer_image][callout_layout]") {
    const CalloutPoint a{100, 50}, c{105, 90};
    const auto e = leader_elbow(a, c, 8, true);
    CHECK(e.x == a.x);
    CHECK(e.y == c.y);
}

TEST_CASE("leader elbow: a level chip puts the elbow on the straight line",
          "[printer_image][callout_layout]") {
    const CalloutPoint a{100, 50}, c{200, 50};
    const auto e = leader_elbow(a, c, 8, true);
    CHECK(e.y == 50);
    CHECK(e.x >= a.x);
    CHECK(e.x <= c.x);
}

TEST_CASE("leader elbow: a left-hand chip mirrors the run", "[printer_image][callout_layout]") {
    const CalloutPoint a{100, 50};
    const auto e = leader_elbow(a, CalloutPoint{0, 80}, 8, true);
    CHECK(e.x == 70);
    CHECK(e.y == 80);
    const auto steep = leader_elbow(a, CalloutPoint{80, 100}, 8, true);
    CHECK(steep.x == 80 + 8);
    CHECK(steep.y == 100);
}

TEST_CASE("leader elbow: bands above and below run 45 degrees, then vertical into the chip",
          "[printer_image][callout_layout]") {
    const CalloutPoint a{100, 50};
    const auto e = leader_elbow(a, CalloutPoint{130, 200}, 8, false);
    CHECK(std::abs(e.x - a.x) == std::abs(e.y - a.y));
    CHECK(e.x == 130);
    CHECK(e.y == 80);
    const auto steep = leader_elbow(a, CalloutPoint{160, 70}, 8, false);
    CHECK(steep.x == 160);
    CHECK(steep.y == 70 - 8);
    const auto above = leader_elbow(a, CalloutPoint{70, 0}, 8, false);
    CHECK(above.x == 70);
    CHECK(above.y == 20);
}

TEST_CASE("leader lines from the layout end level into side chips, vertical into top chips",
          "[printer_image][callout_layout]") {
    auto in = wide();
    in.area_w = 186 + 2 * (70 + in.gap + in.min_line);
    auto l = compute_callout_layout(in);
    REQUIRE(l.mode == CalloutMode::BothSides);
    for (const auto& c : l.chips) {
        INFO(int(c.kind));
        const auto e =
            leader_elbow({c.line_x0, c.line_y0}, {c.line_x1, c.line_y1}, in.min_line, true);
        CHECK(c.line_xm == e.x);
        CHECK(c.line_ym == e.y);
        CHECK(c.line_ym == c.line_y1);
    }
    in.area_w = 240;
    in.area_h = 480;
    l = compute_callout_layout(in);
    REQUIRE((l.mode == CalloutMode::BothSides || l.mode == CalloutMode::OneSide));
    for (const auto& c : l.chips) {
        INFO(int(c.kind));
        const auto e =
            leader_elbow({c.line_x0, c.line_y0}, {c.line_x1, c.line_y1}, in.min_line, false);
        CHECK(c.line_xm == e.x);
        CHECK(c.line_ym == e.y);
        CHECK(c.line_xm == c.line_x1);
    }
}

// ---------------------------------------------------------------------------
// Pinned chips slide apart instead of landing on each other
// ---------------------------------------------------------------------------

namespace {

bool overlaps(const CalloutRect& a, const CalloutRect& b) {
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

void check_apart_and_inside(const CalloutLayout& l, const CalloutLayoutInput& in) {
    for (size_t i = 0; i < l.chips.size(); ++i) {
        INFO("chip " << int(l.chips[i].kind));
        CHECK(inside(l.chips[i].rect, in.area_w, in.area_h));
        for (size_t j = i + 1; j < l.chips.size(); ++j) {
            INFO("vs chip " << int(l.chips[j].kind));
            CHECK_FALSE(overlaps(l.chips[i].rect, l.chips[j].rect));
        }
    }
}

// A K1C 2x2 tile: the merged toolhead chip on the nozzle, the light chip on
// the light, which sits just above and left of it.
CalloutLayoutInput k1c_2x2() {
    auto in = base();
    in.area_w = 240;
    in.area_h = 200;
    in.chip_h = 30;
    in.active = {{CalloutKind::Nozzle, 100, NormPoint{0.513f, 0.279f}},
                 {CalloutKind::Fan, 50, NormPoint{0.488f, 0.206f}},
                 {CalloutKind::Light, 40, NormPoint{0.321f, 0.164f}}};
    in.toolhead = CalloutChipIn{CalloutKind::Toolhead, 184, NormPoint{0.513f, 0.279f}};
    return in;
}

} // namespace

TEST_CASE("pinned: the toolhead chip and the light chip slide apart (K1C 2x2)",
          "[printer_image][callout_layout]") {
    const auto in = k1c_2x2();
    const auto l = compute_callout_layout(in);
    REQUIRE(l.mode == CalloutMode::Pinned);
    REQUIRE(l.toolhead_merged);
    const auto* th = find(l, CalloutKind::Toolhead);
    const auto* light = find(l, CalloutKind::Light);
    REQUIRE(th);
    REQUIRE(light);
    // Placed on their points, the two would collide.
    const int nx = l.image.x + int(0.513f * l.image.w), ny = l.image.y + int(0.279f * l.image.h);
    const int lx = l.image.x + int(0.321f * l.image.w), ly = l.image.y + int(0.164f * l.image.h);
    REQUIRE(overlaps({nx - 92, ny - 15, 184, 30}, {lx - 20, ly - 15, 40, 30}));
    check_apart_and_inside(l, in);
    // Only vertical moves: each chip stays centred on its point's x.
    CHECK(th->rect.x == nx - 92);
    CHECK(light->rect.x == lx - 20);
}

TEST_CASE("pinned: chamber and nozzle chips slide apart (Q2-like)",
          "[printer_image][callout_layout]") {
    auto in = base();
    in.area_w = 240;
    in.area_h = 200;
    in.image_w = 1000;
    in.image_h = 1000;
    in.chip_h = 30;
    in.active = {{CalloutKind::Chamber, 70, NormPoint{0.368f, 0.531f}},
                 {CalloutKind::Nozzle, 70, NormPoint{0.574f, 0.398f}}};
    const auto l = compute_callout_layout(in);
    REQUIRE(l.mode == CalloutMode::Pinned);
    REQUIRE(l.chips.size() == 2);
    // Placed on their points, the two would collide.
    const auto on_point = [&](const CalloutChipIn& c) {
        const int x = l.image.x + int(c.anchor->x * l.image.w);
        const int y = l.image.y + int(c.anchor->y * l.image.h);
        return CalloutRect{x - c.w / 2, y - in.chip_h / 2, c.w, in.chip_h};
    };
    REQUIRE(overlaps(on_point(in.active[0]), on_point(in.active[1])));
    check_apart_and_inside(l, in);
}

TEST_CASE("pinned: a column too tall for a short tile docks its lowest-priority chips",
          "[printer_image][callout_layout]") {
    auto in = base();
    in.area_w = 240;
    in.area_h = 100; // four stacked chips need 132
    in.chip_h = 30;
    in.active = {{CalloutKind::Nozzle, 150, NormPoint{0.513f, 0.279f}},
                 {CalloutKind::Bed, 100, NormPoint{0.46f, 0.57f}},
                 {CalloutKind::Chamber, 90, NormPoint{0.31f, 0.38f}},
                 {CalloutKind::Light, 28, NormPoint{0.32f, 0.16f}}};
    const auto l = compute_callout_layout(in);
    REQUIRE(l.mode == CalloutMode::Pinned);
    REQUIRE(l.chips.size() == 4);
    check_apart_and_inside(l, in);
    // The nozzle keeps its point; the light gives way first, to the bottom row.
    const auto* nozzle = find(l, CalloutKind::Nozzle);
    REQUIRE(nozzle);
    CHECK(nozzle->rect.x == l.image.x + int(0.513f * l.image.w) - 75);
    const auto* light = find(l, CalloutKind::Light);
    REQUIRE(light);
    CHECK(light->rect.y + light->rect.h == in.area_h - in.gap);
}

TEST_CASE("pinned: chips that do not collide stay centred on their points",
          "[printer_image][callout_layout]") {
    auto in = base();
    in.area_w = 240;
    in.area_h = 200;
    in.chip_h = 30;
    // Same column, far apart vertically; and one level with the nozzle, off to the side.
    in.active = {{CalloutKind::Nozzle, 60, NormPoint{0.5f, 0.25f}},
                 {CalloutKind::Bed, 60, NormPoint{0.5f, 0.75f}},
                 {CalloutKind::Light, 30, NormPoint{0.1f, 0.25f}}};
    const auto l = compute_callout_layout(in);
    REQUIRE(l.mode == CalloutMode::Pinned);
    for (const auto& a : in.active) {
        const auto* c = find(l, a.kind);
        REQUIRE(c);
        const int px = l.image.x + int(a.anchor->x * l.image.w);
        const int py = l.image.y + int(a.anchor->y * l.image.h);
        CHECK(c->rect.x == px - a.w / 2);
        CHECK(c->rect.y == py - in.chip_h / 2);
    }
}

TEST_CASE("pinned: a chip on a low point stays clear of the docked row",
          "[printer_image][callout_layout]") {
    auto in = base();
    in.active = {{CalloutKind::Bed, 60, NormPoint{0.5f, 0.9f}},
                 {CalloutKind::Chamber, 50, std::nullopt}};
    const auto l = compute_callout_layout(in);
    REQUIRE(l.mode == CalloutMode::Pinned);
    const auto* chamber = find(l, CalloutKind::Chamber);
    REQUIRE(chamber);
    REQUIRE(chamber->rect.y + chamber->rect.h == in.area_h - in.gap); // docked row
    // On its point the bed chip would sit on the docked chamber chip.
    const int bx = l.image.x + int(0.5f * l.image.w), by = l.image.y + int(0.9f * l.image.h);
    REQUIRE(overlaps({bx - 30, by - in.chip_h / 2, 60, in.chip_h}, chamber->rect));
    check_apart_and_inside(l, in);
}
