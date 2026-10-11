// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_belt_advanced_row_gate.cpp
 * @brief The Advanced panel's Belt Tension row follows belt-compare support.
 *
 * Builds the real advanced_panel.xml and drives the two subjects the row's
 * visibility binding reads: printer_has_accelerometer and
 * printer_supports_belt_compare (1 only on corexy/limited_corexy). The row is
 * shown when both are 1 and hidden when either is 0.
 */

#include "ui_status_pill.h"

#include "../lvgl_ui_test_fixture.h"
#include "../test_fixtures.h"
#include "lvgl/lvgl.h"
#include "setting_group.h"

#include <string>

#include "../catch_amalgamated.hpp"

namespace {

/// Builds the real advanced_panel.xml so the Belt Tension row's visibility
/// binding can be exercised against a live tree, in the same shape
/// production's xml_registration.xml uses (see AdvancedPowerGroupFixture in
/// test_host_power_availability.cpp for the sibling POWER-group case).
class AdvancedBeltRowFixture : public XMLTestFixture {
  public:
    AdvancedBeltRowFixture() : XMLTestFixture() {
        setting_group_register();
        ui_status_pill_register_widget();
        REQUIRE(register_component("setting_group_header"));
        REQUIRE(register_component("setting_action_row"));
        REQUIRE(register_component("beta_feature"));
        REQUIRE(register_component("advanced_panel"));

        // The XML subject registry is process-global and never forgets an
        // entry, so every subject below is a function-local STATIC: a fixture
        // member would dangle after this test and any later consumer of the
        // name dereferences freed memory.
        ensure_subject("show_beta_features", 1);
        ensure_subject("printer_has_accelerometer", 1);
        ensure_subject("printer_supports_belt_compare", 0);

        panel_ = create_component("advanced_panel");
        REQUIRE(panel_ != nullptr);
        row_ = lv_obj_find_by_name(panel_, "row_belt_tension");
        REQUIRE(row_ != nullptr);
        process_lvgl(50);
    }

    lv_subject_t* subject(const char* name) {
        lv_subject_t* subj = lv_xml_get_subject(nullptr, name);
        REQUIRE(subj != nullptr);
        return subj;
    }

    lv_obj_t* panel_ = nullptr;
    lv_obj_t* row_ = nullptr;

  private:
    /// Registers `name` in the global XML scope at `value` when absent,
    /// leaving any earlier registration (this test's or another fixture's)
    /// untouched.
    static void ensure_subject(const char* name, int value) {
        if (!lv_xml_get_subject(nullptr, name)) {
            static lv_subject_t subjects[3];
            static size_t next = 0;
            REQUIRE(next < 3);
            lv_subject_init_int(&subjects[next], value);
            lv_xml_register_subject(nullptr, name, &subjects[next]);
            next++;
        }
    }
};

} // namespace

TEST_CASE_METHOD(AdvancedBeltRowFixture, "Advanced Belt Tension row needs belt-compare support",
                 "[belt][advanced][xml]") {
    auto* accel = subject("printer_has_accelerometer");
    auto* belt = subject("printer_supports_belt_compare");

    // Without belt-path kinematics the row stays hidden even with an
    // accelerometer.
    lv_subject_set_int(accel, 1);
    lv_subject_set_int(belt, 0);
    process_lvgl(10);
    CHECK(lv_obj_has_flag(row_, LV_OBJ_FLAG_HIDDEN));

    // Belt-path kinematics plus an accelerometer shows it.
    lv_subject_set_int(belt, 1);
    process_lvgl(10);
    CHECK_FALSE(lv_obj_has_flag(row_, LV_OBJ_FLAG_HIDDEN));

    // Losing support hides it again.
    lv_subject_set_int(belt, 0);
    process_lvgl(10);
    CHECK(lv_obj_has_flag(row_, LV_OBJ_FLAG_HIDDEN));

    // Support alone is not enough: no accelerometer, no row.
    lv_subject_set_int(belt, 1);
    lv_subject_set_int(accel, 0);
    process_lvgl(10);
    CHECK(lv_obj_has_flag(row_, LV_OBJ_FLAG_HIDDEN));
}
