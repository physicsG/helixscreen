// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui_status_pill.h"

#include "../ui_test_utils.h"
#include "helix-xml/src/xml/lv_xml.h"
#include "lvgl/lvgl.h"
#include "setting_group.h"
#include "test_fixtures.h"

#include "../catch_amalgamated.hpp"

// Recursively find the first lv_label descendant whose text equals `text`.
// Type-guards the get_text() call so non-label nodes don't emit warnings.
static lv_obj_t* find_label_with_text(lv_obj_t* obj, const std::string& text) {
    if (lv_obj_check_type(obj, &lv_label_class) && UITest::get_text(obj) == text)
        return obj;
    uint32_t n = lv_obj_get_child_count(obj);
    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t* found = find_label_with_text(lv_obj_get_child(obj, i), text);
        if (found)
            return found;
    }
    return nullptr;
}

// Fixture that registers the setting_group widget (mirrors SplitButtonXmlFixture).
class SettingGroupFixture : public XMLTestFixture {
  public:
    SettingGroupFixture() : XMLTestFixture() {
        setting_group_register();
        // setting_group_header renders a status_pill badge; register the widget
        // so it instantiates in the header component under test.
        ui_status_pill_register_widget();
    }
};

TEST_CASE_METHOD(SettingGroupFixture, "setting_group: applies card shell", "[setting_group]") {
    auto* group = static_cast<lv_obj_t*>(lv_xml_create(test_screen(), "setting_group", nullptr));
    REQUIRE(group != nullptr);

    // Card fill is opaque (from StyleRole::Card via configure_card).
    REQUIRE(lv_obj_get_style_bg_opa(group, LV_PART_MAIN) == LV_OPA_COVER);
    // Nothing focused: rows draw nothing in the corners, so no clipping layer.
    REQUIRE_FALSE(lv_obj_get_style_clip_corner(group, LV_PART_MAIN));
    // Group is a fixed container, not a scroll area.
    REQUIRE_FALSE(lv_obj_has_flag(group, LV_OBJ_FLAG_SCROLLABLE));
}

TEST_CASE_METHOD(SettingGroupFixture,
                 "setting_group: clips its corners only while a focus ring reaches one",
                 "[setting_group]") {
    auto* group = static_cast<lv_obj_t*>(lv_xml_create(test_screen(), "setting_group", nullptr));
    REQUIRE(group != nullptr);
    REQUIRE(lv_obj_get_style_radius(group, LV_PART_MAIN) > 0);
    auto make_row = [](lv_obj_t* parent) {
        lv_obj_t* row = lv_button_create(parent);
        lv_obj_set_size(row, LV_PCT(100), 40);
        return row;
    };
    lv_obj_t* first = make_row(group);
    lv_obj_t* middle = make_row(group);
    lv_obj_t* wrapper = lv_obj_create(group);
    lv_obj_set_size(wrapper, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(wrapper, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(wrapper, 0, LV_PART_MAIN);
    lv_obj_t* last_nested = make_row(wrapper);
    lv_obj_update_layout(test_screen());

    SECTION("the first row") {
        lv_obj_add_state(first, LV_STATE_FOCUSED);
        REQUIRE(lv_obj_get_style_clip_corner(group, LV_PART_MAIN));
        lv_obj_remove_state(first, LV_STATE_FOCUSED);
        REQUIRE_FALSE(lv_obj_get_style_clip_corner(group, LV_PART_MAIN));
    }
    SECTION("the last row, inside a wrapper") {
        lv_obj_add_state(last_nested, LV_STATE_FOCUSED);
        REQUIRE(lv_obj_get_style_clip_corner(group, LV_PART_MAIN));
    }
    SECTION("a middle row never reaches a corner") {
        lv_obj_add_state(middle, LV_STATE_FOCUSED);
        REQUIRE_FALSE(lv_obj_get_style_clip_corner(group, LV_PART_MAIN));
    }
    SECTION("focus moving off a corner row onto a middle row drops the clip") {
        lv_obj_add_state(first, LV_STATE_FOCUSED);
        lv_obj_add_state(middle, LV_STATE_FOCUSED);
        lv_obj_remove_state(first, LV_STATE_FOCUSED);
        REQUIRE_FALSE(lv_obj_get_style_clip_corner(group, LV_PART_MAIN));
    }
    SECTION("a row added after layout becomes the last row") {
        lv_obj_t* late = make_row(group);
        lv_obj_update_layout(test_screen());
        lv_obj_add_state(late, LV_STATE_FOCUSED);
        REQUIRE(lv_obj_get_style_clip_corner(group, LV_PART_MAIN));
    }
    SECTION("a focused middle row that becomes the last row picks up the clip") {
        lv_obj_add_state(middle, LV_STATE_FOCUSED);
        lv_obj_add_flag(wrapper, LV_OBJ_FLAG_HIDDEN);
        lv_obj_update_layout(test_screen());
        REQUIRE(lv_obj_get_style_clip_corner(group, LV_PART_MAIN));
    }
}

TEST_CASE_METHOD(SettingGroupFixture, "setting_group: divider count skips hidden children",
                 "[setting_group]") {
    auto* group = static_cast<lv_obj_t*>(lv_xml_create(test_screen(), "setting_group", nullptr));
    REQUIRE(group != nullptr);

    // Three visible children -> two dividers (one above each after the first).
    lv_obj_t* a = lv_obj_create(group);
    lv_obj_t* b = lv_obj_create(group);
    lv_obj_t* c = lv_obj_create(group);
    (void)a;
    (void)c;
    process_lvgl(50);
    REQUIRE(setting_group_divider_count(group) == 2);

    // Hide the middle child -> one divider.
    lv_obj_add_flag(b, LV_OBJ_FLAG_HIDDEN);
    process_lvgl(50);
    REQUIRE(setting_group_divider_count(group) == 1);

    // Hide all but one -> zero dividers.
    lv_obj_add_flag(c, LV_OBJ_FLAG_HIDDEN);
    process_lvgl(50);
    REQUIRE(setting_group_divider_count(group) == 0);
}

TEST_CASE_METHOD(SettingGroupFixture, "setting_group_header: renders title text",
                 "[setting_group]") {
    REQUIRE(register_component("setting_group_header"));
    const char* attrs[] = {"title", "DISPLAY", nullptr};
    auto* header = create_component("setting_group_header", attrs);
    REQUIRE(header != nullptr);
    process_lvgl(50);

    // The section title is rendered as a label carrying the passed text.
    REQUIRE(find_label_with_text(header, "DISPLAY") != nullptr);
}

TEST_CASE_METHOD(SettingGroupFixture, "setting_group_header: icon visibility follows hide_icon",
                 "[setting_group]") {
    REQUIRE(register_component("setting_group_header"));

    // hide_icon="false" -> the section icon is visible (no HIDDEN flag).
    {
        const char* attrs[] = {"title", "DISPLAY", "icon", "light", "hide_icon", "false", nullptr};
        auto* header = create_component("setting_group_header", attrs);
        REQUIRE(header != nullptr);
        process_lvgl(50);

        lv_obj_t* icon = lv_obj_find_by_name(header, "section_icon");
        REQUIRE(icon != nullptr);
        REQUIRE_FALSE(lv_obj_has_flag(icon, LV_OBJ_FLAG_HIDDEN));
    }

    // hide_icon="true" -> the section icon is hidden (HIDDEN flag set).
    {
        const char* attrs[] = {"title", "DISPLAY", "icon", "light", "hide_icon", "true", nullptr};
        auto* header = create_component("setting_group_header", attrs);
        REQUIRE(header != nullptr);
        process_lvgl(50);

        lv_obj_t* icon = lv_obj_find_by_name(header, "section_icon");
        REQUIRE(icon != nullptr);
        REQUIRE(lv_obj_has_flag(icon, LV_OBJ_FLAG_HIDDEN));
    }
}

TEST_CASE_METHOD(SettingGroupFixture, "setting_group_header: badge visibility follows hide_badge",
                 "[setting_group]") {
    REQUIRE(register_component("setting_group_header"));

    // hide_badge="false" with a badge_name -> the named count pill is visible.
    {
        const char* attrs[] = {"title",           "FANS",       "badge_name",
                               "fan_count_badge", "badge_text", "3",
                               "hide_badge",      "false",      nullptr};
        auto* header = create_component("setting_group_header", attrs);
        REQUIRE(header != nullptr);
        process_lvgl(50);

        lv_obj_t* badge = lv_obj_find_by_name(header, "fan_count_badge");
        REQUIRE(badge != nullptr);
        REQUIRE_FALSE(lv_obj_has_flag(badge, LV_OBJ_FLAG_HIDDEN));
    }

    // Default (hide_badge omitted -> "true") -> the pill is hidden.
    {
        const char* attrs[] = {"title", "FANS", "badge_name", "fan_count_badge_hidden", nullptr};
        auto* header = create_component("setting_group_header", attrs);
        REQUIRE(header != nullptr);
        process_lvgl(50);

        lv_obj_t* badge = lv_obj_find_by_name(header, "fan_count_badge_hidden");
        REQUIRE(badge != nullptr);
        REQUIRE(lv_obj_has_flag(badge, LV_OBJ_FLAG_HIDDEN));
    }
}

// A section whose every row is hidden hides its header too, and shows it again
// the moment any row returns (Advanced's sections gate rows on capabilities).
TEST_CASE_METHOD(SettingGroupFixture, "setting_group: header follows whether any row shows",
                 "[setting_group]") {
    auto* group = static_cast<lv_obj_t*>(lv_xml_create(test_screen(), "setting_group", nullptr));
    REQUIRE(group != nullptr);
    lv_obj_t* header = lv_obj_create(group);
    lv_obj_set_name(header, "setting_group_header");
    lv_obj_set_height(header, 30);
    lv_obj_t* row_a = lv_obj_create(group);
    lv_obj_set_height(row_a, 40);
    lv_obj_t* row_b = lv_obj_create(group);
    lv_obj_set_height(row_b, 40);
    lv_obj_update_layout(test_screen());
    CHECK_FALSE(lv_obj_has_flag(header, LV_OBJ_FLAG_HIDDEN));

    SECTION("one row still showing keeps the header") {
        lv_obj_add_flag(row_a, LV_OBJ_FLAG_HIDDEN);
        lv_obj_update_layout(test_screen());
        CHECK_FALSE(lv_obj_has_flag(header, LV_OBJ_FLAG_HIDDEN));
        CHECK_FALSE(lv_obj_has_state(group, LV_STATE_USER_1));
    }

    SECTION("every row hidden hides the header and collapses the card") {
        lv_obj_add_flag(row_a, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(row_b, LV_OBJ_FLAG_HIDDEN);
        lv_obj_update_layout(test_screen());
        CHECK(lv_obj_has_flag(header, LV_OBJ_FLAG_HIDDEN));
        CHECK(lv_obj_has_state(group, LV_STATE_USER_1));
        CHECK(lv_obj_get_style_margin_bottom(group, LV_PART_MAIN) == 0);

        // A row coming back brings the header with it.
        lv_obj_remove_flag(row_b, LV_OBJ_FLAG_HIDDEN);
        lv_obj_update_layout(test_screen());
        CHECK_FALSE(lv_obj_has_flag(header, LV_OBJ_FLAG_HIDDEN));
        CHECK_FALSE(lv_obj_has_state(group, LV_STATE_USER_1));
    }

    SECTION("a visible wrapper with nothing inside counts as no row") {
        lv_obj_add_flag(row_a, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_height(row_b, LV_SIZE_CONTENT);
        lv_obj_set_style_pad_all(row_b, 0, LV_PART_MAIN);
        lv_obj_set_style_border_width(row_b, 0, LV_PART_MAIN);
        lv_obj_update_layout(test_screen());
        CHECK(lv_obj_has_flag(header, LV_OBJ_FLAG_HIDDEN));
    }
}
