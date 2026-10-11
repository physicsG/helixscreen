// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_runout_manual_actions_paused.cpp
 * @brief manual_action_row visibility across the print-state / autofeed matrix
 *
 * The runout dialog's Load/Unload/Purge row is the only surface offering a
 * manual load while a runout-paused job is standing. Hiding it on an autofeed
 * backend assumes Resume always reloads by itself; when the firmware declines
 * that feed there is then no way to recover the job from the dialog at all.
 *
 * `print_state_enum` carries PrintJobState (PRINTING=1, PAUSED=2), NOT the
 * PrintState lifecycle -- the two do not share numbering.
 */

#include "ui_runout_guidance_modal.h"

#include "../lvgl_ui_test_fixture.h"
#include "helix-xml/src/xml/lv_xml.h"
#include "printer_state.h"
#include "ui_update_queue.h"

#include <lvgl.h>

#include "../catch_amalgamated.hpp"

namespace {

bool row_hidden(lv_obj_t* root) {
    lv_obj_t* row = lv_obj_find_by_name(root, "manual_action_row");
    REQUIRE(row != nullptr);
    return lv_obj_has_flag(row, LV_OBJ_FLAG_HIDDEN);
}

} // namespace

TEST_CASE_METHOD(LVGLUITestFixture, "Runout manual actions survive a paused autofeed runout",
                 "[ui][runout][modal][snapmaker]") {
    // The modal owns runout_autofeed_capable; it is static and outlives any one
    // instance, so the case hands it back cleared.
    RunoutGuidanceModal modal;
    modal.set_autofeed_capable(false);

    auto* root =
        static_cast<lv_obj_t*>(lv_xml_create(test_screen(), "runout_guidance_modal", nullptr));
    REQUIRE(root != nullptr);

    lv_subject_t* wire_subject = lv_xml_get_subject(nullptr, "print_state_enum");
    REQUIRE(wire_subject != nullptr);
    const int wire_before = lv_subject_get_int(wire_subject);

    const auto apply = [&](bool autofeed_capable, helix::PrintJobState wire) {
        modal.set_autofeed_capable(autofeed_capable);
        lv_subject_set_int(wire_subject, static_cast<int>(wire));
        helix::ui::UpdateQueue::instance().drain();
    };

    SECTION("PAUSED on an autofeed backend still offers them") {
        // The U1 shape, and the whole point: recovers_filament_on_resume() is
        // true there, so a runout-paused user had no Load button on the only
        // dialog standing.
        apply(/*autofeed_capable=*/true, helix::PrintJobState::PAUSED);
        CHECK_FALSE(row_hidden(root));
    }

    SECTION("PAUSED on a basic-sensor backend still offers them") {
        apply(/*autofeed_capable=*/false, helix::PrintJobState::PAUSED);
        CHECK_FALSE(row_hidden(root));
    }

    SECTION("PRINTING hides them whatever the backend") {
        // Load/Unload/Purge mid-print destroys the print, and
        // refuse_if_printing() refuses them anyway.
        apply(/*autofeed_capable=*/true, helix::PrintJobState::PRINTING);
        CHECK(row_hidden(root));
        apply(/*autofeed_capable=*/false, helix::PrintJobState::PRINTING);
        CHECK(row_hidden(root));
    }

    SECTION("an idle autofeed runout still hides them") {
        // Not the recovery case: Resume is not on offer and the backend loads on
        // its own. Pinned so the paused carve-out cannot be widened into
        // "always shown" without this failing.
        apply(/*autofeed_capable=*/true, helix::PrintJobState::STANDBY);
        CHECK(row_hidden(root));
    }

    SECTION("an idle basic-sensor runout offers them") {
        apply(/*autofeed_capable=*/false, helix::PrintJobState::STANDBY);
        CHECK_FALSE(row_hidden(root));
    }
    modal.set_autofeed_capable(false);
    lv_subject_set_int(wire_subject, wire_before);
    lv_obj_delete(root);
}
