// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#if HELIX_HAS_PLUGINS

#include "../lvgl_test_fixture.h"
#include "../test_helpers/log_capture.h"
#include "../test_helpers/plugin_test_support.h"
#include "../test_helpers/scoped_pointer_indev.h"
#include "lua_bindings.h"
#include "plugin_host.h"
#include "plugin_table.h"
#include "theme_manager.h"
#include "widgets/table/lv_table_private.h" // row_h: lv_table has no cell-area getter

#include <string>
#include <vector>

#include "../catch_amalgamated.hpp"

using namespace helix::plugin;
using namespace helix::plugin::test;

namespace {

std::vector<Installer> table_installers() {
    return {&install_ui_bindings, &install_table_bindings};
}

lv_obj_t* make_table(lv_obj_t* parent, const char* name) {
    // The host's one-time registration, so the element exists exactly as the app adds it.
    register_plugin_event_callback();
    const char* attrs[] = {"name", name, "width", "400", "height", "300", nullptr};
    auto* t = static_cast<lv_obj_t*>(lv_xml_create(parent, "plugin_table", attrs));
    REQUIRE(t);
    lv_obj_update_layout(t);
    return t;
}

std::string cell(lv_obj_t* t, uint32_t row, uint32_t col) {
    const char* v = lv_table_get_cell_value(t, row, col);
    return v ? v : "";
}

/// A tap on cell (row, col), as lv_table reports one that did not scroll.
void tap(lv_obj_t* t, uint16_t row, uint16_t col) {
    uint32_t r = 0;
    uint32_t c = 0;
    lv_table_get_selected_cell(t, &r, &c);
    if (r == row && c == col)
        lv_obj_send_event(t, LV_EVENT_VALUE_CHANGED, nullptr);
    else
        lv_table_set_selected_cell(t, row, col);
}

/// The screen point at the center of table row @p row, first column.
lv_point_t row_center(lv_obj_t* t, uint32_t row) {
    lv_area_t a;
    lv_obj_get_content_coords(t, &a);
    const auto* tbl = reinterpret_cast<const lv_table_t*>(t);
    int32_t y = a.y1;
    for (uint32_t r = 0; r < row; ++r)
        y += tbl->row_h[r];
    return {a.x1 + tbl->col_w[0] / 2, y + tbl->row_h[row] / 2};
}

const char* kTasks = R"(
    activated = {}
    tasks = helix.ui.table("tasks", {
        columns = {{title = "Task", width = 50}, {title = "Interval"}, {title = "Since"}},
        on_activate = function(key) activated[#activated + 1] = key end,
    })
    tasks:set_rows({
        {key = "rails-lubrication", "Lubricate rails", "100 h", "120 h"},
        {key = "belts", "Check belts", "250 h", 180},
        {key = "filter", "Replace filter", "500 h"},
    })
)";

} // namespace

TEST_CASE("table column widths and data rows", "[plugin][lua_bindings_table]") {
    std::vector<TableColumn> cols = {{"A", 50}, {"B", 0}, {"C", 0}};
    CHECK(table_column_widths(cols, 400) == std::vector<int32_t>{200, 100, 100});
    CHECK(table_column_widths({{"A", 0}, {"B", 0}}, 300) == std::vector<int32_t>{150, 150});
    CHECK(table_column_widths({{"A", 100}, {"B", 0}}, 300) == std::vector<int32_t>{300, 1});
    CHECK(table_column_widths({}, 300).empty());

    CHECK(table_data_row(0, 1, 3) == -1); // the header
    CHECK(table_data_row(1, 0, 3) == 0);
    CHECK(table_data_row(3, 2, 3) == 2);
    CHECK(table_data_row(4, 0, 3) == -1);
    CHECK(table_data_row(LV_TABLE_CELL_NONE, 0, 3) == -1);
    CHECK(table_data_row(1, LV_TABLE_CELL_NONE, 3) == -1);
}

TEST_CASE_METHOD(LVGLTestFixture, "a plugin table shows its columns and rows",
                 "[plugin][lua_bindings_table]") {
    BoundRuntime b(table_installers());
    lv_obj_t* early = make_table(test_screen(), "test-plugin__tasks");
    REQUIRE(b.t.run(kTasks));
    // A widget created after the data renders it too.
    lv_obj_t* late = make_table(test_screen(), "test-plugin__tasks");
    for (lv_obj_t* t : {early, late}) {
        CHECK(lv_table_get_row_count(t) == 4);
        CHECK(lv_table_get_column_count(t) == 3);
        CHECK(cell(t, 0, 0) == "Task");
        CHECK(cell(t, 0, 2) == "Since");
        CHECK(cell(t, 1, 0) == "Lubricate rails");
        CHECK(cell(t, 2, 2) == "180");
        CHECK(cell(t, 3, 2) == ""); // a missing cell reads empty
        CHECK(lv_table_get_column_width(t, 0) == lv_obj_get_content_width(t) / 2);
        // Rows are measured in the face they are drawn in.
        CHECK(lv_obj_get_style_text_font(t, LV_PART_ITEMS) == theme_manager_get_font("font_body"));
    }
    CHECK(table_instance_count("test-plugin__tasks") == 2);

    // An empty list leaves the header alone.
    REQUIRE(b.t.run("tasks:set_rows({})"));
    CHECK(lv_table_get_row_count(early) == 1);
    CHECK(cell(early, 0, 1) == "Interval");
}

TEST_CASE_METHOD(LVGLTestFixture, "a tap activates the row's key, never the header",
                 "[plugin][lua_bindings_table]") {
    BoundRuntime b(table_installers());
    lv_obj_t* t = make_table(test_screen(), "test-plugin__tasks");
    REQUIRE(b.t.run(kTasks));
    tap(t, 0, 1);
    CHECK(b.t.global("activated") != "nil");
    REQUIRE(b.t.run("n = #activated"));
    CHECK(b.t.global("n") == "0");

    tap(t, 1, 2); // any cell of the row identifies it
    REQUIRE(b.t.run("got = activated[1]; sel = tasks:selected()"));
    CHECK(b.t.global("got") == "rails-lubrication");
    CHECK(b.t.global("sel") == "rails-lubrication");

    // A row added ahead moves it down; the same item still answers with its key and
    // keeps the selection.
    REQUIRE(b.t.run(R"(tasks:set_rows({
        {key = "bed", "Clean bed", "50 h", "10 h"},
        {key = "rails-lubrication", "Lubricate rails", "100 h", "120 h"},
    }))"));
    REQUIRE(b.t.run("sel = tasks:selected()"));
    CHECK(b.t.global("sel") == "rails-lubrication");
    tap(t, 2, 0);
    REQUIRE(b.t.run("got = activated[#activated]"));
    CHECK(b.t.global("got") == "rails-lubrication");

    // Removing the selected item clears the selection rather than moving it.
    REQUIRE(b.t.run(R"(tasks:set_rows({{key = "bed", "Clean bed", "50 h", "10 h"}}))"));
    REQUIRE(b.t.run("sel = tasks:selected()"));
    CHECK(b.t.global("sel") == "nil");
}

TEST_CASE_METHOD(LVGLTestFixture, "table:select selects by key", "[plugin][lua_bindings_table]") {
    BoundRuntime b(table_installers());
    REQUIRE(b.t.run(kTasks));
    REQUIRE(b.t.run(R"(a = tasks:select("belts"); s1 = tasks:selected()
                       b = tasks:select("nope"); s2 = tasks:selected()
                       c = tasks:select(nil); s3 = tasks:selected())"));
    CHECK(b.t.global("a") == "true");
    CHECK(b.t.global("s1") == "belts");
    CHECK(b.t.global("b") == "false");
    CHECK(b.t.global("s2") == "belts");
    CHECK(b.t.global("c") == "true");
    CHECK(b.t.global("s3") == "nil");
    // Selecting from Lua is not a tap: nothing activates.
    REQUIRE(b.t.run("n = #activated"));
    CHECK(b.t.global("n") == "0");
}

TEST_CASE_METHOD(LVGLTestFixture, "unloading a plugin empties its tables",
                 "[plugin][lua_bindings_table]") {
    lv_obj_t* t = make_table(test_screen(), "test-plugin__tasks");
    {
        BoundRuntime b(table_installers());
        REQUIRE(b.t.run(kTasks));
        REQUIRE(lv_table_get_row_count(t) == 4);
    }
    CHECK(table_model("test-plugin__tasks") == nullptr);
    CHECK(lv_table_get_row_count(t) == 1);
    CHECK(cell(t, 0, 0) == "");
    tap(t, 0, 0); // a tap after unload reaches nothing
    lv_obj_delete(t);
    CHECK(table_instance_count("test-plugin__tasks") == 0);
}

TEST_CASE_METHOD(LVGLTestFixture, "plugin table arguments are validated",
                 "[plugin][lua_bindings_table]") {
    const char* bad[] = {
        R"(helix.ui.table("t"))",
        R"(helix.ui.table("Bad Name", {columns = {{title = "A"}}}))",
        R"(helix.ui.table("t", {columns = {}}))",
        R"(helix.ui.table("t", {columns = {{}}}))",
        R"(helix.ui.table("t", {columns = {{title = "A", width = 0}}}))",
        R"(helix.ui.table("t", {columns = {{title = "A", width = 101}}}))",
        R"(helix.ui.table("t", {columns = {{title = "A", width = 60}, {title = "B", width = 41}}}))",
        R"(helix.ui.table("t", {columns = {{title = "A"}}, on_activate = 3}))",
        R"(local c = {} for i = 1, 9 do c[i] = {title = "c"} end helix.ui.table("t", {columns = c}))",
        R"(helix.ui.table("t", {columns = {{title = "A"}}}):set_rows({{"no key"}}))",
        R"(helix.ui.table("t", {columns = {{title = "A"}}}):set_rows({{key = "", "x"}}))",
        R"(helix.ui.table("t", {columns = {{title = "A"}}}):set_rows({{key = "k", "x"}, {key = "k", "y"}}))",
        R"(helix.ui.table("t", {columns = {{title = "A"}}}):set_rows({{key = "k", "x", "y"}}))",
        R"(helix.ui.table("t", {columns = {{title = "A"}}}):set_rows({{key = "k", {}}}))",
        R"(helix.ui.table("t", {columns = {{title = "A"}}}):set_rows({{key = "k", string.rep("x", 257)}}))",
        R"(helix.ui.table("t", {columns = {{title = "A"}}}):set_rows({{key = string.rep("k", 129)}}))",
        R"(local r = {} for i = 1, 501 do r[i] = {key = "k" .. i} end
           helix.ui.table("t", {columns = {{title = "A"}}}):set_rows(r))",
    };
    for (const char* lua : bad) {
        INFO(lua);
        BoundRuntime b(table_installers());
        CHECK_FALSE(b.t.run(lua));
        CHECK_FALSE(b.t.rt->faulted());
        const TableModel* m = table_model("test-plugin__t");
        CHECK((m == nullptr || m->rows.empty()));
    }
}

TEST_CASE_METHOD(LVGLTestFixture, "table rows count against the plugin memory budget",
                 "[plugin][lua_bindings_table]") {
    LuaRuntime::Limits limits;
    limits.memory_bytes = 512 * 1024;
    BoundRuntime b(table_installers(), {}, {}, {}, limits);
    REQUIRE(b.t.run(R"(t = helix.ui.table("t", {columns = {{title = "A"}, {title = "B"}}}))"));
    helix::LogCapture logs;
    CHECK_FALSE(b.t.run(R"(
        local rows = {}
        for i = 1, 500 do rows[i] = {key = "k" .. i, string.rep("y", 256), string.rep("z", 256)} end
        t:set_rows(rows)
    )"));
    CHECK(logs.count_containing("table:set_rows: plugin memory budget exceeded") >= 1);
    CHECK_FALSE(b.t.rt->faulted());
}

TEST_CASE_METHOD(LVGLTestFixture, "table changes are refused in a check",
                 "[plugin][lua_bindings_table][check]") {
    BoundRuntime b(table_installers());
    REQUIRE(b.t.run(kTasks));
    REQUIRE(b.t.run(R"(helix.ui.on("check", function() tasks:set_rows({}) return true end)
                       helix.ui.on("reads", function() return tasks:selected() == nil end))"));
    helix::LogCapture logs;
    CHECK_FALSE(call_ui_handler_sync(*b.t.rt, "check", {}, {}));
    CHECK(logs.has_line_with({"table:set_rows", "not allowed in a menu check"}));
    CHECK(table_model("test-plugin__tasks")->rows.size() == 3);
    bool visible = false;
    CHECK(call_ui_handler_sync(*b.t.rt, "reads", {}, [&](lua_State* co, int n) {
        visible = n >= 1 && lua_toboolean(co, -n);
    }));
    CHECK(visible);
}

TEST_CASE_METHOD(LVGLTestFixture, "a drag across rows of a table that fits activates nothing",
                 "[plugin][lua_bindings_table]") {
    BoundRuntime b(table_installers());
    lv_obj_t* t = make_table(test_screen(), "test-plugin__tasks");
    REQUIRE(b.t.run(kTasks));
    lv_obj_update_layout(t);
    const lv_point_t a = row_center(t, 1);
    const lv_point_t c = row_center(t, 2);
    lv_area_t box;
    lv_obj_get_coords(t, &box);
    const lv_point_t blank = {a.x, box.y2 - 4};
    REQUIRE(blank.y > row_center(t, 3).y + 20); // below the last row: blank table space
    REQUIRE(lv_obj_get_scroll_bottom(t) <= 0);  // every row fits, so nothing scrolls

    helix_test::ScopedPointerIndev indev;
    indev.press(a.x, a.y);
    // Steps shorter than the scroll limit, so no single read counts as a move.
    for (int32_t y = a.y + 4; y < blank.y; y += 4)
        indev.move(a.x, y);
    REQUIRE(c.y < blank.y); // the drag crossed row 2 on its way down
    indev.release(blank.x, blank.y);
    CHECK(lv_indev_get_scroll_obj(indev.indev()) == nullptr);

    REQUIRE(b.t.run("n = #activated; sel = tasks:selected()"));
    CHECK(b.t.global("n") == "0");
    CHECK(b.t.global("sel") == "nil");
}

TEST_CASE_METHOD(LVGLTestFixture, "a pointer tap on a table row activates it",
                 "[plugin][lua_bindings_table]") {
    BoundRuntime b(table_installers());
    lv_obj_t* t = make_table(test_screen(), "test-plugin__tasks");
    REQUIRE(b.t.run(kTasks));
    lv_obj_update_layout(t);
    const lv_point_t p = row_center(t, 2);

    helix_test::ScopedPointerIndev indev;
    indev.press(p.x, p.y);
    indev.move(p.x + 2, p.y + 2); // finger jitter well inside the scroll limit
    indev.release(p.x + 2, p.y + 2);

    REQUIRE(b.t.run("got = activated[1]; n = #activated"));
    CHECK(b.t.global("n") == "1");
    CHECK(b.t.global("got") == "belts");
}

#endif // HELIX_HAS_PLUGINS
