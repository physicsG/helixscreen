// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_pause_markers_ui.cpp
 * @brief Scheduled-pause markers on the progress surfaces (prestonbrown/helixscreen#1509).
 *
 * Three invariants, each pinned by rendered pixels rather than by internals:
 *   1. a tick lands at the pause's fraction ON THE AXIS THE BAR FILLS ON —
 *      publishing the same pause list under the two axes must move the tick,
 *      and by exactly the fraction gap, so marker and fill can never disagree
 *      (#1510's rule);
 *   2. markers are absent when no scan ever ran (external start, no gcode)
 *      and when the print changed under a late scan — degrade to absent,
 *      never to wrong;
 *   3. a new print clears the list.
 *
 * Pixels are compared as snapshot diffs against the same widget rendered with
 * no markers, which keeps the assertions independent of any theme color.
 */

#include "ui_pause_markers.h"

#include "../lvgl_test_fixture.h"
#include "gcode_pause_scan.h"
#include "printer_state.h"

#include <algorithm>
#include <lvgl.h>
#include <memory>
#include <string>
#include <vector>

#include "../catch_amalgamated.hpp"

using namespace helix;
using Catch::Approx;

namespace {

struct PauseMarkerFixture : public LVGLTestFixture {
    PauseMarkerFixture() {
        state_.init_subjects(false);
    }

    ~PauseMarkerFixture() override {
        state_.deinit_subjects();
    }

    /// Report an active print of @p filename the way Moonraker does, so the
    /// pause list's identity gate has something to match.
    void report_printing(const std::string& filename) {
        nlohmann::json status = {{"print_stats", {{"filename", filename}, {"state", "printing"}}}};
        state_.update_from_status(status);
    }

    void publish(const std::vector<helix::gcode::ScheduledPause>& pauses,
                 helix::gcode::ProgressAxis axis, const std::string& filename) {
        state_.print_state().set_scheduled_pauses(pauses, axis, filename);
    }

    static helix::gcode::ScheduledPause pause_at(float byte_fraction, float slicer_fraction) {
        helix::gcode::ScheduledPause p{};
        p.file_offset = static_cast<uint64_t>(byte_fraction * 1000);
        p.byte_fraction = byte_fraction;
        p.slicer_fraction = slicer_fraction;
        p.layer_index = 5;
        p.kind = helix::gcode::PauseKind::FilamentChange;
        return p;
    }

    PrinterState state_;
};

/// Compare a snapshot against a same-size baseline buffer; return the sorted
/// distinct x columns where any pixel differs.
std::vector<int> diff_columns(lv_draw_buf_t* snapshot, const lv_draw_buf_t* baseline) {
    std::vector<int> columns;
    REQUIRE(snapshot != nullptr);
    REQUIRE(baseline != nullptr);
    REQUIRE(snapshot->header.w == baseline->header.w);
    REQUIRE(snapshot->header.h == baseline->header.h);
    const uint32_t w = snapshot->header.w;
    const uint32_t h = snapshot->header.h;
    const uint32_t s_stride = snapshot->header.stride ? snapshot->header.stride : w * 4;
    const uint32_t b_stride = baseline->header.stride ? baseline->header.stride : w * 4;
    for (uint32_t x = 0; x < w; ++x) {
        bool differs = false;
        for (uint32_t y = 0; y < h && !differs; ++y) {
            const uint8_t* s = snapshot->data + y * s_stride + x * 4;
            const uint8_t* b = baseline->data + y * b_stride + x * 4;
            if (s[0] != b[0] || s[1] != b[1] || s[2] != b[2] || s[3] != b[3]) {
                differs = true;
            }
        }
        if (differs) {
            columns.push_back(static_cast<int>(x));
        }
    }
    return columns;
}

/// Draw hooks use absolute screen coordinates, while a snapshot buffer's
/// origin is the object's coords shrunk by its ext_draw_size on each side
/// (lv_snapshot_reshape_draw_buf). The expansion is exactly why the buffer is
/// wider than the object, so it is derived here rather than read from LVGL's
/// private headers: absolute x -> the snapshot column it lands in.
int snapshot_column_for_abs_x(const lv_draw_buf_t* snap, lv_obj_t* obj, int32_t abs_x) {
    lv_area_t coords;
    lv_obj_get_coords(obj, &coords);
    const int ext = (static_cast<int>(snap->header.w) - lv_obj_get_width(obj)) / 2;
    return abs_x - coords.x1 + ext;
}

int column_mid(const std::vector<int>& columns) {
    return (*std::min_element(columns.begin(), columns.end()) +
            *std::max_element(columns.begin(), columns.end())) /
           2;
}

lv_draw_buf_t* take_snapshot(lv_obj_t* obj) {
    return lv_snapshot_take(obj, LV_COLOR_FORMAT_ARGB8888);
}

lv_obj_t* make_bar(lv_obj_t* screen, PrinterState& ps) {
    lv_obj_t* bar = lv_bar_create(screen);
    lv_obj_set_size(bar, 200, 16);
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, 0, LV_ANIM_OFF);
    helix::ui::attach_bar_pause_markers(bar, ps);
    lv_obj_update_layout(bar);
    return bar;
}

} // namespace

TEST_CASE_METHOD(PauseMarkerFixture, "Pause markers: linear bar ticks follow the print's axis",
                 "[pause_markers][print_status][1509]") {
    lv_obj_t* bar = make_bar(test_screen(), state_);
    report_printing("multicolor.gcode");
    lv_draw_buf_t* baseline = take_snapshot(bar);
    REQUIRE(baseline);

    // The two fractions differ on purpose: 0.4 slicer vs 0.6 byte. Which one
    // the tick uses is the whole axis invariant.
    const std::vector<helix::gcode::ScheduledPause> pauses{pause_at(0.6f, 0.4f)};

    SECTION("tick lands at the fraction of the file's axis") {
        publish(pauses, helix::gcode::ProgressAxis::SlicerTime, "multicolor.gcode");
        lv_draw_buf_t* marked = take_snapshot(bar);
        const std::vector<int> slicer_cols = diff_columns(marked, baseline);
        lv_draw_buf_destroy(marked);
        REQUIRE_FALSE(slicer_cols.empty());

        publish(pauses, helix::gcode::ProgressAxis::BytePosition, "multicolor.gcode");
        marked = take_snapshot(bar);
        const std::vector<int> byte_cols = diff_columns(marked, baseline);
        lv_draw_buf_destroy(marked);
        REQUIRE_FALSE(byte_cols.empty());

        // 0.2 of a 200px-wide bar apart; both ticks share the same pads, so
        // the distance is the pure axis difference.
        const int distance = column_mid(byte_cols) - column_mid(slicer_cols);
        CHECK(distance == Approx(40).margin(4));

        // Absolute placement too: the slicer tick sits at 0.4 of the bar's
        // track, computed the way the draw hook computes it.
        lv_area_t coords;
        lv_obj_get_coords(bar, &coords);
        const int32_t pad_left = lv_obj_get_style_pad_left(bar, LV_PART_MAIN);
        const int32_t pad_right = lv_obj_get_style_pad_right(bar, LV_PART_MAIN);
        const int32_t track = lv_area_get_width(&coords) - pad_left - pad_right;
        const int expected = snapshot_column_for_abs_x(
            baseline, bar, coords.x1 + pad_left + static_cast<int32_t>(0.4f * track));
        CHECK(column_mid(slicer_cols) == Approx(expected).margin(3));
    }

    SECTION("multiple pauses draw multiple ticks") {
        publish({pause_at(0.25f, 0.25f), pause_at(0.75f, 0.75f)},
                helix::gcode::ProgressAxis::BytePosition, "multicolor.gcode");
        lv_draw_buf_t* marked = take_snapshot(bar);
        const std::vector<int> columns = diff_columns(marked, baseline);
        lv_draw_buf_destroy(marked);

        REQUIRE_FALSE(columns.empty());
        const int x_min = *std::min_element(columns.begin(), columns.end());
        const int x_max = *std::max_element(columns.begin(), columns.end());
        // Two ticks ~100px apart on a 200px bar.
        CHECK(x_min < 60);
        CHECK(x_max > 140);
        // Each tick is thin (2px + antialias), so the pair leaves a gap.
        CHECK(x_max - x_min > 60);
    }

    lv_draw_buf_destroy(baseline);
    lv_obj_delete(bar);
}

TEST_CASE_METHOD(PauseMarkerFixture, "Pause markers: degrade to absent, never to wrong",
                 "[pause_markers][print_status][1509]") {
    lv_obj_t* bar = make_bar(test_screen(), state_);
    report_printing("current.gcode");
    lv_draw_buf_t* baseline = take_snapshot(bar);
    REQUIRE(baseline);

    SECTION("a scan for a different print stays hidden") {
        // A scan completing after a print switch publishes under the OLD
        // file's name; the identity gate must keep it off the new print.
        publish({pause_at(0.5f, 0.5f)}, helix::gcode::ProgressAxis::BytePosition, "previous.gcode");
        lv_draw_buf_t* marked = take_snapshot(bar);
        CHECK(diff_columns(marked, baseline).empty());
        lv_draw_buf_destroy(marked);
    }

    SECTION("a new print clears the list and the markers") {
        publish({pause_at(0.5f, 0.5f)}, helix::gcode::ProgressAxis::BytePosition, "current.gcode");
        lv_draw_buf_t* marked = take_snapshot(bar);
        REQUIRE_FALSE(diff_columns(marked, baseline).empty());
        lv_draw_buf_destroy(marked);

        state_.print_state().reset_for_new_print();

        lv_draw_buf_t* cleared = take_snapshot(bar);
        CHECK(diff_columns(cleared, baseline).empty());
        lv_draw_buf_destroy(cleared);
        CHECK(state_.print_state().get_scheduled_pauses().empty());
    }

    SECTION("version subject signals publish and clear") {
        const int v0 = lv_subject_get_int(state_.print_state().get_pause_markers_version_subject());
        publish({pause_at(0.5f, 0.5f)}, helix::gcode::ProgressAxis::BytePosition, "current.gcode");
        const int v1 = lv_subject_get_int(state_.print_state().get_pause_markers_version_subject());
        CHECK(v1 > v0);

        state_.print_state().reset_for_new_print();
        const int v2 = lv_subject_get_int(state_.print_state().get_pause_markers_version_subject());
        CHECK(v2 > v1);
    }

    lv_draw_buf_destroy(baseline);
    lv_obj_delete(bar);
}

TEST_CASE_METHOD(PauseMarkerFixture, "Pause markers: arc draws a tick along the sweep",
                 "[pause_markers][print_status][1509]") {
    lv_obj_t* arc = lv_arc_create(test_screen());
    lv_obj_set_size(arc, 100, 100);
    lv_arc_set_bg_angles(arc, 135, 45);
    lv_arc_set_value(arc, 0);
    helix::ui::attach_arc_pause_markers(arc, state_);
    lv_obj_update_layout(arc);

    report_printing("arcprint.gcode");
    lv_draw_buf_t* baseline = take_snapshot(arc);
    REQUIRE(baseline);

    // One pause at half the job on the byte axis: angle 135 + 0.5*270 = 270
    // degrees, the top-center of the dial (the gap is at the bottom).
    publish({pause_at(0.5f, 0.5f)}, helix::gcode::ProgressAxis::BytePosition, "arcprint.gcode");

    lv_draw_buf_t* marked = take_snapshot(arc);
    REQUIRE(marked);
    const std::vector<int> columns = diff_columns(marked, baseline);
    lv_area_t coords;
    lv_obj_get_coords(arc, &coords);
    const int expected = snapshot_column_for_abs_x(baseline, arc, coords.x1 + 50);
    lv_draw_buf_destroy(marked);
    lv_draw_buf_destroy(baseline);

    REQUIRE_FALSE(columns.empty());
    const int x_min = *std::min_element(columns.begin(), columns.end());
    const int x_max = *std::max_element(columns.begin(), columns.end());
    // One thin tick at the center column (2px width + antialias slack).
    CHECK(x_min >= expected - 3);
    CHECK(x_max <= expected + 3);

    lv_obj_delete(arc);
}
