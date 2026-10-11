// Copyright (C) 2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_ams_unit_disconnected.cpp
 * @brief A unit its backend reports offline reads disconnected on every
 *        surface that names it: the per-unit subject, the viewed-unit and
 *        whole-system subjects, and the unit card that binds them.
 */

#include "ui_update_queue.h"

#include "../lvgl_ui_test_fixture.h"
#include "ams_backend_mock.h"
#include "ams_state.h"
#include "ams_types.h"
#include "helix-xml/src/xml/lv_xml.h"

#include <lvgl/lvgl.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "../catch_amalgamated.hpp"

using namespace helix;

namespace {

struct UnitShape {
    bool connected;
    bool absent;
};

class ConnectionMock : public AmsBackendMock {
  public:
    explicit ConnectionMock(std::vector<UnitShape> shapes)
        : AmsBackendMock(static_cast<int>(shapes.size()) * 4), shapes_(std::move(shapes)) {}

    AmsSystemInfo get_system_info() const override {
        AmsSystemInfo info = AmsBackendMock::get_system_info();
        info.units.clear();
        for (size_t u = 0; u < shapes_.size(); ++u) {
            AmsUnit unit;
            unit.unit_index = static_cast<int>(u);
            unit.name = "conn_unit_" + std::to_string(u);
            unit.slot_count = 4;
            unit.first_slot_global_index = static_cast<int>(u) * 4;
            unit.connected = shapes_[u].connected;
            unit.absent = shapes_[u].absent;
            for (int s = 0; s < 4; ++s) {
                SlotInfo slot;
                slot.slot_index = s;
                slot.global_index = unit.first_slot_global_index + s;
                unit.slots.push_back(slot);
            }
            info.units.push_back(unit);
        }
        info.total_slots = static_cast<int>(shapes_.size()) * 4;
        return info;
    }

  private:
    std::vector<UnitShape> shapes_;
};

void install(std::vector<UnitShape> shapes) {
    AmsState::instance().deinit_subjects();
    auto mock = std::make_unique<ConnectionMock>(std::move(shapes));
    REQUIRE(mock->start().success());
    AmsState::instance().set_backend(std::move(mock));
    AmsState::instance().init_subjects(true);
    AmsState::instance().sync_from_backend();
    helix::ui::UpdateQueue::instance().drain();
    REQUIRE(AmsState::instance().get_backend()->get_system_info().units.size() >= 2);
}

int subject_int(const std::string& name) {
    lv_subject_t* subj = lv_xml_get_subject(nullptr, name.c_str());
    REQUIRE(subj != nullptr);
    return lv_subject_get_int(subj);
}

} // namespace

TEST_CASE("AmsUnit reads connected until a backend says otherwise", "[ams][disconnected]") {
    CHECK(AmsUnit{}.connected);
}

TEST_CASE_METHOD(LVGLUITestFixture, "A disconnected unit publishes its own subject",
                 "[ams][disconnected]") {
    // Unit 2 is an absent placeholder: it has its own "Not connected" state and
    // must not also read disconnected.
    install({{true, false}, {false, false}, {false, true}});

    CHECK(subject_int(AmsState::unit_disconnected_subject_name(0)) == 0);
    CHECK(subject_int(AmsState::unit_disconnected_subject_name(1)) == 1);
    CHECK(subject_int(AmsState::unit_disconnected_subject_name(2)) == 0);
    CHECK(subject_int("ams_all_units_disconnected") == 0);

    SECTION("the viewed-unit subject follows the unit being viewed") {
        CHECK(subject_int("ams_viewed_unit_disconnected") == 0);
        AmsState::instance().set_viewed_unit(1);
        CHECK(subject_int("ams_viewed_unit_disconnected") == 1);
        AmsState::instance().set_viewed_unit(0);
        CHECK(subject_int("ams_viewed_unit_disconnected") == 0);
        AmsState::instance().set_viewed_unit(-1);
        CHECK(subject_int("ams_viewed_unit_disconnected") == 0);
    }

    SECTION(
        "the unit card fades its contents, stays tappable and shows its chip at full contrast") {
        const std::string absent = AmsState::unit_absent_subject_name(1);
        const std::string disc = AmsState::unit_disconnected_subject_name(1);
        const char* attrs[] = {"absent",     absent.c_str(), "disconnected",
                               disc.c_str(), nullptr,        nullptr};
        auto* card = static_cast<lv_obj_t*>(lv_xml_create(test_screen(), "ams_unit_card", attrs));
        REQUIRE(card != nullptr);
        CHECK_FALSE(lv_obj_has_state(card, LV_STATE_DISABLED));
        CHECK(lv_obj_get_style_opa(card, LV_PART_MAIN) == LV_OPA_COVER);
        lv_obj_t* bars = lv_obj_find_by_name(card, "bars_container");
        REQUIRE(bars != nullptr);
        CHECK(lv_obj_get_style_opa(bars, LV_PART_MAIN) < LV_OPA_COVER);
        lv_obj_t* header = lv_obj_find_by_name(card, "unit_header");
        REQUIRE(header != nullptr);
        CHECK(lv_obj_get_style_opa(header, LV_PART_MAIN) < LV_OPA_COVER);
        lv_obj_t* chip = lv_obj_find_by_name(card, "unit_disconnected_chip");
        REQUIRE(chip != nullptr);
        CHECK_FALSE(lv_obj_has_flag(chip, LV_OBJ_FLAG_HIDDEN));
        CHECK(lv_obj_get_style_opa(chip, LV_PART_MAIN) == LV_OPA_COVER);
        CHECK(lv_obj_get_style_opa(lv_obj_get_parent(chip), LV_PART_MAIN) == LV_OPA_COVER);
        lv_obj_t* count = lv_obj_find_by_name(card, "slot_count");
        REQUIRE(count != nullptr);
        CHECK(lv_obj_has_flag(count, LV_OBJ_FLAG_HIDDEN));
        lv_obj_delete(card);
    }

    SECTION("a connected unit's card shows no chip") {
        const std::string absent = AmsState::unit_absent_subject_name(0);
        const std::string disc = AmsState::unit_disconnected_subject_name(0);
        const char* attrs[] = {"absent",     absent.c_str(), "disconnected",
                               disc.c_str(), nullptr,        nullptr};
        auto* card = static_cast<lv_obj_t*>(lv_xml_create(test_screen(), "ams_unit_card", attrs));
        REQUIRE(card != nullptr);
        CHECK_FALSE(lv_obj_has_state(card, LV_STATE_DISABLED));
        CHECK(lv_obj_get_style_opa(card, LV_PART_MAIN) == LV_OPA_COVER);
        lv_obj_t* bars = lv_obj_find_by_name(card, "bars_container");
        REQUIRE(bars != nullptr);
        CHECK(lv_obj_get_style_opa(bars, LV_PART_MAIN) == LV_OPA_COVER);
        lv_obj_t* chip = lv_obj_find_by_name(card, "unit_disconnected_chip");
        REQUIRE(chip != nullptr);
        CHECK(lv_obj_has_flag(chip, LV_OBJ_FLAG_HIDDEN));
        lv_obj_delete(card);
    }

    AmsState::instance().set_viewed_unit(-1);
}

TEST_CASE_METHOD(LVGLUITestFixture, "Every present unit offline is the whole system offline",
                 "[ams][disconnected]") {
    install({{false, false}, {false, true}});
    CHECK(subject_int("ams_all_units_disconnected") == 1);
}
