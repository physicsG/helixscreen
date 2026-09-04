// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../test_helpers/printer_state_test_access.h"
#include "../ui_test_utils.h"
#include "app_globals.h"
#include "printer_state.h"

#include "../catch_amalgamated.hpp"
#include "hv/json.hpp"

using namespace helix;

// ============================================================================
// pause_resume.is_paused Tracking Tests
// ============================================================================
// Tests use update_from_status() directly since update_from_notification() uses
// lv_async_call() which requires pumping the LVGL timer.

TEST_CASE("PrinterState tracks pause_resume.is_paused", "[printer-state][pause]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(false);

    SECTION("defaults to false before any status update") {
        REQUIRE_FALSE(state.is_paused());
    }

    SECTION("becomes true when Klipper reports is_paused:true") {
        json status = {{"pause_resume", {{"is_paused", true}}}};
        state.update_from_status(status);
        REQUIRE(state.is_paused());
    }

    SECTION("becomes false when Klipper reports is_paused:false") {
        // First set to true
        json status_true = {{"pause_resume", {{"is_paused", true}}}};
        state.update_from_status(status_true);
        REQUIRE(state.is_paused());

        // Then clear it
        json status_false = {{"pause_resume", {{"is_paused", false}}}};
        state.update_from_status(status_false);
        REQUIRE_FALSE(state.is_paused());
    }

    SECTION("ignores pause_resume update with missing is_paused field") {
        json status = {{"pause_resume", {{"other_field", 42}}}};
        state.update_from_status(status);
        REQUIRE_FALSE(state.is_paused()); // unchanged from default
    }

    SECTION("ignores pause_resume update with non-boolean is_paused") {
        json status = {{"pause_resume", {{"is_paused", 1}}}};
        state.update_from_status(status);
        REQUIRE_FALSE(state.is_paused()); // unchanged: integer, not boolean
    }
}
