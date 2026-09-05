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

#include "../test_fixtures.h"
#include "helix-xml/src/xml/lv_xml.h"
#include "printer_state.h"
#include "ui_update_queue.h"

#include <lvgl.h>

#include "../catch_amalgamated.hpp"

namespace {

/// File-scope, not a local: Catch2 re-enters the TEST_CASE body once per
/// SECTION, and a stack subject would be re-registered at a new address while
/// the previous section's observers still pointed at the dead one.
lv_subject_t& autofeed_subject() {
    static lv_subject_t s{};
    static bool inited = false;
    if (!inited) {
        lv_subject_init_int(&s, 0);
        inited = true;
    }
    return s;
}

bool row_hidden(lv_obj_t* root) {
    lv_obj_t* row = lv_obj_find_by_name(root, "manual_action_row");
    REQUIRE(row != nullptr);
    return lv_obj_has_flag(row, LV_OBJ_FLAG_HIDDEN);
}

} // namespace

TEST_CASE_METHOD(XMLTestFixture, "Runout manual actions survive a paused autofeed runout",
                 "[ui][runout][modal][snapmaker]") {
    // `runout_autofeed_capable` is normally a RunoutGuidanceModal static
    // registered into this component's scope. That registration is one-shot and
    // order-dependent, so the test owns a subject of the same name in the global
    // scope instead -- the cond resolver falls back to global when the component
    // scope has no entry, which is exactly the case with no modal constructed.
    lv_subject_t& autofeed = autofeed_subject();
    REQUIRE(lv_xml_register_subject(nullptr, "runout_autofeed_capable", &autofeed) ==
            LV_RESULT_OK);

    REQUIRE(register_component("runout_guidance_modal"));
    lv_obj_t* root = create_component("runout_guidance_modal");
    REQUIRE(root != nullptr);

    lv_subject_t* wire_subject = lv_xml_get_subject(nullptr, "print_state_enum");
    REQUIRE(wire_subject != nullptr);

    const auto apply = [&](bool autofeed_capable, helix::PrintJobState wire) {
        lv_subject_set_int(&autofeed, autofeed_capable ? 1 : 0);
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
}
