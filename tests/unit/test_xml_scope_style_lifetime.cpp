// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_xml_scope_style_lifetime.cpp
 * @brief A component scope must outlive every widget still using its styles.
 *
 * Widgets hold RAW `lv_style_t*` into their scope's `style_ll`. Re-registering a
 * component replaces its scope and retires the old one, so a widget that
 * outlived its view root - reparented onto a layer, or an overlay left standing -
 * is pointing into freed storage the moment that scope is released.
 */

#include "ui_utils.h"

#include "../test_fixtures.h"
#include "helix-xml/src/xml/lv_xml.h"
#include "helix-xml/src/xml/lv_xml_component.h"
#include "lvgl/lvgl.h"
#include "lvgl/lvgl_private.h"

#include "../catch_amalgamated.hpp"

namespace {

// One component owning one style, so the retired scope has style storage to free.
const char* SCOPE_FIXTURE_XML = R"(
<component>
  <styles>
    <style name="scope_probe_style" bg_color="0xff0000" pad_all="7"/>
  </styles>
  <view extends="lv_obj">
    <lv_obj name="probe_child">
      <style name="scope_probe_style"/>
    </lv_obj>
  </view>
</component>
)";

/// The style a widget resolved, or nullptr when it carries none.
const lv_style_t* first_style_of(lv_obj_t* obj) {
    for (uint32_t i = 0; i < obj->style_cnt; i++) {
        if (!obj->styles[i].is_theme && !obj->styles[i].is_local) {
            return obj->styles[i].style;
        }
    }
    return nullptr;
}

} // namespace

TEST_CASE("a widget on a layer keeps its component scope alive", "[xml][style][lifetime]") {
    XMLTestFixture fixture;

    REQUIRE(lv_xml_register_component_from_data("scope_probe", SCOPE_FIXTURE_XML) == LV_RESULT_OK);

    auto* root = static_cast<lv_obj_t*>(lv_xml_create(lv_screen_active(), "scope_probe", nullptr));
    REQUIRE(root != nullptr);
    lv_obj_t* child = lv_obj_find_by_name(root, "probe_child");
    REQUIRE(child != nullptr);

    const lv_style_t* style = first_style_of(child);
    REQUIRE(style != nullptr);
    REQUIRE(style->sentinel == LV_STYLE_SENTINEL_VALUE);

    // The production path that strands a widget: safe_clean_children() parks each
    // child on lv_layer_top() and hands it to lv_obj_delete_async(). Nothing here
    // pumps LVGL, so the child stays there - still holding the scope's style -
    // while instance_cnt falls to zero with the view root.
    helix::ui::safe_clean_children(root);
    CHECK(lv_obj_get_parent(child) == lv_layer_top());
    lv_obj_delete(root);

    // Replacing the component retires the scope that owns `style`.
    REQUIRE(lv_xml_register_component_from_data("scope_probe", SCOPE_FIXTURE_XML) == LV_RESULT_OK);

    // Freeing it would leave this pointer dangling. LVGL stamps every live style
    // with the sentinel, so a freed-and-reused one reads as anything else.
    CHECK(style->sentinel == LV_STYLE_SENTINEL_VALUE);

    // And the style must still be usable, not merely unreclaimed.
    lv_style_value_t v{};
    CHECK(lv_style_get_prop(style, LV_STYLE_PAD_TOP, &v) == LV_STYLE_RES_FOUND);
    CHECK(v.num == 7);

    lv_obj_delete(child);
}
