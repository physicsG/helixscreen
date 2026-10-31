// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_state_binding_composition.cpp
 * @brief A widget state driven by several subjects must survive all of them
 *
 * Two <bind_state_if_*> on one state do not compose. Each observer fires only
 * when ITS subject changes and asserts both polarities when it does, so the last
 * subject to move decides the state and a false condition CLEARS what another
 * binding set. On the filament panel that left Load enabled through an active
 * print: the panel had published filament_load_disabled=1, and a later
 * filament_operation_in_progress 1->0 wiped the disabled state off the button.
 *
 * The single <bind_state_if cond="a or b or c"> re-evaluates the whole
 * expression whenever ANY referenced subject changes, which is what this pins.
 */

#include "../test_fixtures.h"
#include "helix-xml/src/xml/lv_xml.h"
#include "ui_update_queue.h"

#include <lvgl.h>

#include "../catch_amalgamated.hpp"

namespace {

/// File-scope: Catch2 re-enters the body once per SECTION, and a stack subject
/// would be re-registered at a new address while the previous section's
/// observers still pointed at the dead one.
lv_subject_t& subj(int which) {
    static lv_subject_t s[3]{};
    static bool inited = false;
    if (!inited) {
        for (auto& one : s) {
            lv_subject_init_int(&one, 0);
        }
        inited = true;
    }
    return s[which];
}

bool disabled(lv_obj_t* btn) {
    return lv_obj_has_state(btn, LV_STATE_DISABLED);
}

} // namespace

TEST_CASE_METHOD(XMLTestFixture, "A multi-subject disabled state is not clobbered by a sibling",
                 "[ui][xml][state_binding][filament]") {
    lv_subject_t& safety = subj(0);
    lv_subject_t& in_flight = subj(1);
    lv_subject_t& load_disabled = subj(2);
    REQUIRE(lv_xml_register_subject(nullptr, "filament_safety_warning_visible", &safety) ==
            LV_RESULT_OK);
    REQUIRE(lv_xml_register_subject(nullptr, "filament_operation_in_progress", &in_flight) ==
            LV_RESULT_OK);
    REQUIRE(lv_xml_register_subject(nullptr, "filament_load_disabled", &load_disabled) ==
            LV_RESULT_OK);

    // The production markup, not a stand-in: whatever filament_panel.xml says
    // about btn_load is what gets exercised.
    REQUIRE(register_component("filament_panel"));
    lv_obj_t* root = create_component("filament_panel");
    REQUIRE(root != nullptr);
    lv_obj_t* btn = lv_obj_find_by_name(root, "btn_load");
    REQUIRE(btn != nullptr);

    const auto settle = [] { helix::ui::UpdateQueue::instance().drain(); };

    SECTION("a print gate outlives an unrelated operation finishing") {
        // THE REGRESSION. An op runs and ends while a print still blocks Load.
        // Under two bindings the in_flight observer's 1->0 fire removed
        // LV_STATE_DISABLED outright, and the button came back live even though
        // filament_load_disabled was still 1 -- a guaranteed backend refusal.
        lv_subject_set_int(&load_disabled, 1);
        lv_subject_set_int(&in_flight, 1);
        settle();
        REQUIRE(disabled(btn));

        lv_subject_set_int(&in_flight, 0);
        settle();
        CHECK(disabled(btn));
    }

    SECTION("each subject alone disables") {
        for (lv_subject_t* s : {&safety, &in_flight, &load_disabled}) {
            lv_subject_set_int(&safety, 0);
            lv_subject_set_int(&in_flight, 0);
            lv_subject_set_int(&load_disabled, 0);
            settle();
            REQUIRE_FALSE(disabled(btn));

            lv_subject_set_int(s, 1);
            settle();
            CHECK(disabled(btn));
        }
    }

    SECTION("clearing every subject re-enables") {
        lv_subject_set_int(&safety, 1);
        lv_subject_set_int(&in_flight, 1);
        lv_subject_set_int(&load_disabled, 1);
        settle();
        REQUIRE(disabled(btn));

        lv_subject_set_int(&safety, 0);
        lv_subject_set_int(&in_flight, 0);
        lv_subject_set_int(&load_disabled, 0);
        settle();
        CHECK_FALSE(disabled(btn));
    }
}
