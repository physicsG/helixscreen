// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_icon_picker.h"

#include "../lvgl_test_fixture.h"

#include <memory>
#include <string>
#include <vector>

#include "../catch_amalgamated.hpp"

using namespace helix::ui;

namespace {

const char* const kIcons[] = {"fan", "flash", "radiator"};

int outlined(lv_obj_t* grid) {
    int hit = -1;
    for (uint32_t i = 0; i < lv_obj_get_child_count(grid); ++i) {
        if (lv_obj_get_style_border_width(lv_obj_get_child(grid, i), LV_PART_MAIN) > 0) {
            REQUIRE(hit == -1);
            hit = static_cast<int>(i);
        }
    }
    return hit;
}

} // namespace

TEST_CASE_METHOD(LVGLTestFixture, "icon grid: one cell per icon, selected one outlined",
                 "[icon_picker]") {
    lv_obj_t* grid = lv_obj_create(test_screen());
    populate_icon_grid(grid, kIcons, 3, "flash", [](const char*) {});
    CHECK(lv_obj_get_child_count(grid) == 3);
    CHECK(outlined(grid) == 1);
}

TEST_CASE_METHOD(LVGLTestFixture,
                 "icon grid: a tap reports the icon name and refresh moves the outline",
                 "[icon_picker]") {
    lv_obj_t* grid = lv_obj_create(test_screen());
    std::vector<std::string> picked;
    populate_icon_grid(grid, kIcons, 3, "fan", [&](const char* n) { picked.push_back(n); });
    CHECK(outlined(grid) == 0);

    lv_obj_send_event(lv_obj_get_child(grid, 2), LV_EVENT_CLICKED, nullptr);
    REQUIRE(picked.size() == 1);
    CHECK(picked[0] == "radiator");

    refresh_icon_grid(grid, "radiator");
    CHECK(outlined(grid) == 2);
    refresh_icon_grid(grid, "not-an-icon");
    CHECK(outlined(grid) == -1);
}

TEST_CASE_METHOD(LVGLTestFixture, "icon grid: deleting the grid frees the cells cleanly",
                 "[icon_picker]") {
    lv_obj_t* grid = lv_obj_create(test_screen());
    // Every cell holds a copy of the callback; the token lives as long as any copy does.
    auto token = std::make_shared<int>(0);
    std::weak_ptr<int> alive = token;
    populate_icon_grid(grid, kIcons, 3, "", [token](const char*) {});
    token.reset();
    REQUIRE_FALSE(alive.expired());

    lv_obj_delete(grid);
    REQUIRE(alive.expired());
}
