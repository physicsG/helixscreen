// Copyright (C) 2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_print_status_progress_text.cpp
 * @brief The progress card's text subjects follow the print's timings and speed.
 */

#include "ui_panel_print_status.h"

#include "../test_helpers/print_status_panel_fixture.h"
#include "print_progress_text.h"

#include <string>

#include "../catch_amalgamated.hpp"

using print_status_panel_test::PrintStatusPanelFixture;

namespace {

std::string subject_text(const char* name) {
    lv_subject_t* subject = lv_xml_get_subject(nullptr, name);
    REQUIRE(subject != nullptr);
    return lv_subject_get_string(subject);
}

} // namespace

TEST_CASE_METHOD(PrintStatusPanelFixture,
                 "Print status: elapsed and remaining text follow a running print",
                 "[print_status][progress_text]") {
    state().update_from_status({{"print_stats", {{"state", "printing"}, {"filename", "a.gcode"}}}});
    helix::ui::UpdateQueue::instance().drain();
    state().update_from_status({{"print_stats", {{"total_duration", 3720.0}}}});
    helix::ui::UpdateQueue::instance().drain();

    CHECK(subject_text("print_elapsed") == "1h 02m");
}

TEST_CASE_METHOD(PrintStatusPanelFixture, "Print status: speed text follows the speed factor",
                 "[print_status][progress_text]") {
    state().update_from_status({{"gcode_move", {{"speed_factor", 1.5}}}});
    helix::ui::UpdateQueue::instance().drain();

    CHECK(subject_text("print_speed_text") == "150%");
}
