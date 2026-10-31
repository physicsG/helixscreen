// SPDX-License-Identifier: GPL-3.0-or-later

// ui_filament_path_canvas_set/get_slot_upstream: the caption store behind the
// upstream caption a fed lane's column draws. The getter is the read-back a
// debug session or ctl probe uses, so it is pinned to the setter's contract:
// copy-in, empty when never set, cleared by NULL, bounded by the store.

#include "ui_filament_path_canvas.h"

#include "../lvgl_test_fixture.h"
#include "ams_types.h"

#include <string>

#include "../catch_amalgamated.hpp"

TEST_CASE_METHOD(LVGLTestFixture, "FilamentPath: upstream caption set, read and cleared",
                 "[filament_path][upstream]") {
    lv_obj_t* path = ui_filament_path_canvas_create(test_screen());
    REQUIRE(path != nullptr);
    char buf[64];

    SECTION("never set reads empty") {
        ui_filament_path_canvas_get_slot_upstream(path, 0, buf, sizeof(buf));
        CHECK(std::string(buf).empty());
    }

    SECTION("set reads back, NULL clears") {
        ui_filament_path_canvas_set_slot_upstream(path, 0, "ACE 2 \xc2\xb7 3");
        ui_filament_path_canvas_get_slot_upstream(path, 0, buf, sizeof(buf));
        CHECK(std::string(buf) == "ACE 2 \xc2\xb7 3");
        // A different slot is untouched.
        ui_filament_path_canvas_get_slot_upstream(path, 1, buf, sizeof(buf));
        CHECK(std::string(buf).empty());

        ui_filament_path_canvas_set_slot_upstream(path, 0, nullptr);
        ui_filament_path_canvas_get_slot_upstream(path, 0, buf, sizeof(buf));
        CHECK(std::string(buf).empty());
    }

    SECTION("a caption longer than the store is truncated, not overrun") {
        ui_filament_path_canvas_set_slot_upstream(path, 2,
                                                  "0123456789012345678901234567890123456789");
        ui_filament_path_canvas_get_slot_upstream(path, 2, buf, sizeof(buf));
        // 24 bytes of store: 23 characters and the terminator.
        CHECK(std::string(buf) == "01234567890123456789012");
    }

    SECTION("out-of-range slots are refused both ways") {
        ui_filament_path_canvas_set_slot_upstream(path, helix::kAmsMaxSlots, "X");
        ui_filament_path_canvas_get_slot_upstream(path, helix::kAmsMaxSlots, buf, sizeof(buf));
        CHECK(std::string(buf).empty());
        ui_filament_path_canvas_set_slot_upstream(path, -1, "X");
        ui_filament_path_canvas_get_slot_upstream(path, -1, buf, sizeof(buf));
        CHECK(std::string(buf).empty());
    }

    // Setting a caption marks the canvas dirty; give a sized canvas one render
    // pass so the PARALLEL painter runs over the stored captions.
    ui_filament_path_canvas_set_slot_count(path, 4);
    ui_filament_path_canvas_set_topology(path, static_cast<int>(helix::PathTopology::PARALLEL));
    ui_filament_path_canvas_set_slot_upstream(path, 1, "ACE 1");
    lv_obj_set_size(path, 400, 300);
    lv_obj_update_layout(path);
    process_lvgl(50);

    lv_obj_delete(path);
    process_lvgl(30);
}
