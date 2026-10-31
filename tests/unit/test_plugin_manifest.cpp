// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#if HELIX_HAS_PLUGINS

#include "plugin_manifest.h"
#include "plugin_permissions.h"

#include <algorithm>

#include "../catch_amalgamated.hpp"

using namespace helix::plugin;

namespace {
bool has_error_containing(const ManifestParse& r, const std::string& needle) {
    for (const auto& e : r.errors) {
        if (e.find(needle) != std::string::npos)
            return true;
    }
    return false;
}

ManifestParse with_setting(const std::string& setting) {
    return parse_manifest(R"({"id":"ab","name":"n","version":"1","settings":[)" + setting + "]}");
}

ManifestParse with_widgets(const std::string& widgets) {
    return parse_manifest(R"({"id":"ab","name":"n","version":"1","widgets":[)" + widgets + "]}");
}

constexpr const char* kOneWidget = R"("widgets":[{"id":"ab__w","name":"W","component":"ab__c"}])";

ManifestParse with_menu_items(const std::string& items, bool widget = false) {
    return parse_manifest(R"({"id":"ab","name":"n","version":"1",)" +
                          (widget ? std::string(kOneWidget) + "," : std::string()) +
                          R"("menu_items":[)" + items + "]}");
}

std::string spool_items(int n, const char* menu = "spool") {
    std::string out;
    for (int i = 0; i < n; ++i)
        out += std::string(i ? "," : "") + R"({"id":"i)" + std::to_string(i) + R"(","menu":")" +
               menu + R"(","label":"L","handler":"h"})";
    return out;
}
} // namespace

TEST_CASE("plugin ids", "[plugin][manifest]") {
    CHECK(is_valid_plugin_id("orca-cal"));
    CHECK(is_valid_plugin_id("ab"));
    CHECK_FALSE(is_valid_plugin_id("a"));
    CHECK_FALSE(is_valid_plugin_id("orca_cal"));
    CHECK_FALSE(is_valid_plugin_id("Orca"));
    CHECK_FALSE(is_valid_plugin_id("1orca"));
    CHECK_FALSE(is_valid_plugin_id(std::string(33, 'a')));
    CHECK(is_valid_plugin_id(std::string(32, 'a')));
}

TEST_CASE("owned names need the id, the separator and a rest", "[plugin][manifest]") {
    CHECK(is_owned_name("orca-cal", "orca-cal__status"));
    CHECK(is_owned_name("ams", "ams__x"));
    // A single underscore is the app's namespace, not the plugin's.
    CHECK_FALSE(is_owned_name("orca-cal", "orca-cal_status"));
    CHECK_FALSE(is_owned_name("ams", "ams_device_operations"));
    CHECK_FALSE(is_owned_name("extruder", "extruder_target"));
    CHECK_FALSE(is_owned_name("orca-cal", "orca-cal__"));
    CHECK_FALSE(is_owned_name("orca-cal", "orca-calx__status"));
    CHECK_FALSE(is_owned_name("orca", "orca-cal__status"));
    CHECK_FALSE(is_owned_name("orca-cal", "status"));
    CHECK(plugin_owned_name("orca-cal", "status") == "orca-cal__status");
    CHECK(owner_of("orca-cal__my_status") == "orca-cal");
    CHECK(owner_of("orca-cal_status").empty());
    CHECK(owner_of("nounderscore").empty());
    CHECK(local_part_of("orca-cal__my_status") == "my_status");
    CHECK(local_part_of("orca-cal__a__b") == "a__b");
    CHECK(local_part_of("orca-cal_status").empty());
    CHECK(local_part_of("orca-cal__").empty());
}

TEST_CASE("a complete manifest parses", "[plugin][manifest]") {
    auto r = parse_manifest(R"({
      "id": "orca-cal", "name": "Orca Calibration", "version": "1.2.0",
      "author": "someone", "description": "d", "helix_version": ">=1.1",
      "permissions": ["gcode", "moonraker_write"], "memory_mb": 4,
      "settings": [
        {"key": "companion", "type": "string", "label": "Companion"},
        {"key": "auto_apply", "type": "bool", "label": "Auto", "default": false},
        {"key": "step", "type": "int", "label": "Step", "min": 1, "max": 20, "default": 5},
        {"key": "ratio", "type": "float", "label": "Ratio", "min": 0.5, "max": 1.5, "default": 0.95},
        {"key": "test", "type": "enum", "label": "Test",
         "options": ["temperature", "flow"], "default": "flow"},
        {"key": "ping", "type": "action", "label": "Ping", "callback": "orca-cal__ping"},
        {"key": "state", "type": "info", "label": "State", "subject": "orca-cal__state"}
      ],
      "settings_overlay": "orca-cal__settings",
      "widgets": [{"id": "orca-cal__launch", "name": "Launch", "icon": "tune",
                   "description": "Start", "component": "orca-cal__widget",
                   "colspan": 1, "rowspan": 1, "max_colspan": 2}]
    })");
    REQUIRE(r.errors.empty());
    REQUIRE(r.manifest);
    CHECK(r.manifest->id == "orca-cal");
    CHECK(r.manifest->helix_version == ">=1.1");
    CHECK(r.manifest->memory_mb == 4);
    CHECK(r.manifest->permissions == PermissionSet{Permission::Gcode, Permission::MoonrakerWrite});
    CHECK(r.manifest->settings_overlay == "orca-cal__settings");
    REQUIRE(r.manifest->widgets.size() == 1);
    CHECK(r.manifest->widgets[0].id == "orca-cal__launch");
    CHECK(r.manifest->widgets[0].max_colspan == 4); // two cells, in tracks
    REQUIRE(r.manifest->settings.size() == 7);
    CHECK(r.manifest->settings[2].type == SettingType::Int);
    CHECK(r.manifest->settings[2].default_value == 5);
    CHECK(r.manifest->settings[3].max == 1.5);
    CHECK(r.manifest->settings[4].options == std::vector<std::string>{"temperature", "flow"});
    CHECK(r.manifest->settings[5].callback == "orca-cal__ping");
}

TEST_CASE("memory_mb defaults to 2 and is bounded", "[plugin][manifest]") {
    std::string base = R"({"id":"ab","name":"n","version":"1")";
    CHECK(parse_manifest(base + "}").manifest->memory_mb == 2);
    CHECK(parse_manifest(base + R"(,"memory_mb":64})").manifest->memory_mb == 64);
    CHECK(has_error_containing(parse_manifest(base + R"(,"memory_mb":0})"), "memory_mb"));
    CHECK(has_error_containing(parse_manifest(base + R"(,"memory_mb":65})"), "memory_mb"));
    CHECK(has_error_containing(parse_manifest(base + R"(,"memory_mb":"4"})"), "memory_mb"));
}

TEST_CASE("manifest errors", "[plugin][manifest]") {
    CHECK(has_error_containing(parse_manifest("{not json"), "not valid JSON"));
    CHECK(has_error_containing(parse_manifest("[]"), "must be an object"));
    CHECK(has_error_containing(parse_manifest(R"({"name":"n","version":"1"})"), "'id'"));
    CHECK(has_error_containing(parse_manifest(R"({"id":"Bad","name":"n","version":"1"})"), "'id'"));
    CHECK(has_error_containing(parse_manifest(R"({"id":"ab","version":"1"})"), "'name'"));
    CHECK(has_error_containing(parse_manifest(R"({"id":"ab","name":"n"})"), "'version'"));
    CHECK(
        has_error_containing(parse_manifest(R"({"id":"ab","name":"n","version":1})"), "'version'"));
    CHECK(has_error_containing(
        parse_manifest(R"({"id":"ab","name":"n","version":"1","permissions":["root"]})"),
        "unknown permission 'root'"));
    CHECK(has_error_containing(
        parse_manifest(R"({"id":"ab","name":"n","version":"1","permissions":"gcode"})"),
        "'permissions'"));
    CHECK(has_error_containing(
        parse_manifest(R"({"id":"ab","name":"n","version":"1","settings_overlay":"x_view"})"),
        "settings_overlay"));
    CHECK_FALSE(parse_manifest(R"({"id":"ab","name":"n"})").manifest);
}

TEST_CASE("display fields are single-line and length-capped", "[plugin][manifest]") {
    // The consent dialog prints name/version/author as its first line, so a
    // control character in one of them could forge dialog text, and a runaway
    // length could push the real permission lines off the visible body.
    CHECK(has_error_containing(parse_manifest(R"({"id":"ab","name":"Wea\nther","version":"1"})"),
                               "'name'"));
    CHECK(has_error_containing(parse_manifest(R"({"id":"ab","name":"Wea\tther","version":"1"})"),
                               "'name'"));
    CHECK(has_error_containing(
        parse_manifest(
            R"({"id":"ab","name":"Weather\nThis plugin asks for no special permissions.","version":"1"})"),
        "'name'"));
    CHECK(has_error_containing(parse_manifest(R"({"id":"ab","name":"n","version":"1\n0"})"),
                               "'version'"));
    CHECK(has_error_containing(
        parse_manifest(R"({"id":"ab","name":"n","version":"1","author":"a\u0001b"})"), "'author'"));
    CHECK(has_error_containing(
        parse_manifest(R"({"id":"ab","name":"n","version":"1","author":"a\u007fb"})"), "'author'"));

    // Caps: name 48, version 32, author 64 bytes; at the cap parses, past it fails.
    const std::string name48(48, 'x');
    const std::string name49(49, 'x');
    const std::string ver32(32, '1');
    const std::string ver33(33, '1');
    const std::string author64(64, 'a');
    const std::string author65(65, 'a');
    CHECK(parse_manifest(R"({"id":"ab","name":")" + name48 + R"(","version":")" + ver32 +
                         R"(","author":")" + author64 + R"("})")
              .manifest.has_value());
    CHECK(has_error_containing(
        parse_manifest(R"({"id":"ab","name":")" + name49 + R"(","version":"1"})"), "'name'"));
    CHECK(has_error_containing(
        parse_manifest(R"({"id":"ab","name":"n","version":")" + ver33 + R"("})"), "'version'"));
    CHECK(has_error_containing(
        parse_manifest(R"({"id":"ab","name":"n","version":"1","author":")" + author65 + R"("})"),
        "'author'"));

    // The cap counts UTF-8 bytes, and the message says so: 16 three-byte glyphs fit
    // in 48, 17 do not.
    std::string cjk16, cjk17;
    for (int i = 0; i < 16; ++i)
        cjk16 += "\u6f22";
    cjk17 = cjk16 + "\u6f22";
    CHECK(parse_manifest(R"({"id":"ab","name":")" + cjk16 + R"(","version":"1"})")
              .manifest.has_value());
    CHECK(has_error_containing(
        parse_manifest(R"({"id":"ab","name":")" + cjk17 + R"(","version":"1"})"), "48 bytes"));
}

TEST_CASE("setting declaration errors", "[plugin][manifest]") {
    CHECK(has_error_containing(with_setting(R"({"key":"k","type":"colour","label":"l"})"), "type"));
    CHECK(has_error_containing(with_setting(R"({"key":"K!","type":"bool","label":"l"})"), "key"));
    CHECK(has_error_containing(with_setting(R"({"key":"k","type":"bool"})"), "label"));
    CHECK(has_error_containing(
        with_setting(R"({"key":"k","type":"int","label":"l","min":5,"max":5})"), "min"));
    CHECK(has_error_containing(
        with_setting(R"({"key":"k","type":"int","label":"l","min":1,"max":5,"default":9})"),
        "default"));
    CHECK(has_error_containing(
        with_setting(R"({"key":"k","type":"int","label":"l","min":1,"max":5,"default":2.5})"),
        "default"));
    CHECK(has_error_containing(
        with_setting(R"({"key":"k","type":"enum","label":"l","options":[]})"), "options"));
    CHECK(has_error_containing(
        with_setting(R"({"key":"k","type":"enum","label":"l","options":["a"],"default":"b"})"),
        "default"));
    // The generated dropdown joins options with '\n', so one containing a
    // newline would split into wrong entries.
    CHECK(has_error_containing(
        with_setting(R"({"key":"k","type":"enum","label":"l","options":["a\nb"]})"), "options"));
    CHECK(has_error_containing(
        with_setting(R"({"key":"k","type":"action","label":"l","callback":"other_x"})"),
        "callback"));
    CHECK(
        has_error_containing(with_setting(R"({"key":"k","type":"info","label":"l"})"), "subject"));
    CHECK(has_error_containing(with_setting(R"({"key":"k","type":"bool","label":"l","default":1})"),
                               "default"));
    CHECK(has_error_containing(
        with_setting(R"({"key":"k","type":"string","label":"l","default":1})"), "default"));
    CHECK(has_error_containing(
        with_setting(
            R"({"key":"k","type":"bool","label":"l"},{"key":"k","type":"bool","label":"l"})"),
        "duplicate"));
    CHECK(has_error_containing(with_setting(R"("notanobject")"), "must be an object"));
}

TEST_CASE("permissions", "[plugin][manifest]") {
    CHECK(permission_from_string("gcode") == Permission::Gcode);
    CHECK(permission_from_string("moonraker_write") == Permission::MoonrakerWrite);
    CHECK(permission_from_string("http") == Permission::Http);
    CHECK(permission_from_string("storage") == Permission::Storage);
    CHECK_FALSE(permission_from_string("GCODE"));
    CHECK(std::string(permission_name(Permission::MoonrakerWrite)) == "moonraker_write");

    PermissionSet granted{Permission::Gcode};
    CHECK(permission_growth(granted, {Permission::Gcode}).empty());
    CHECK(permission_growth(granted, {}).empty());
    CHECK(permission_growth(granted, {Permission::Http, Permission::Gcode}) ==
          std::vector<Permission>{Permission::Http});

    CHECK(is_readonly_moonraker_method("printer.objects.query"));
    CHECK(is_readonly_moonraker_method("machine.system_info"));
    CHECK(is_readonly_moonraker_method("server.temperature_store"));
    // The HelixPrint plugin's reports a plugin may read; its cleanup is not one.
    CHECK(is_readonly_moonraker_method("server.helix.storage"));
    CHECK(is_readonly_moonraker_method("server.helix.filament_usage"));
    CHECK_FALSE(is_readonly_moonraker_method("server.helix.storage_cleanup"));
    CHECK_FALSE(is_readonly_moonraker_method("printer.gcode.script"));
    CHECK_FALSE(is_readonly_moonraker_method("server.files.delete_file"));
    CHECK_FALSE(is_readonly_moonraker_method("printer.objects.query.extra"));
}

TEST_CASE("manifest widgets parse with cell spans", "[plugin][manifest]") {
    auto r = with_widgets(R"({"id":"ab__launch","name":"Launch","icon":"tune",
        "description":"Start","component":"ab__widget","colspan":1,"rowspan":1,
        "max_colspan":2,"max_rowspan":1})");
    REQUIRE(r.manifest);
    REQUIRE(r.manifest->widgets.size() == 1);
    const auto& w = r.manifest->widgets[0];
    CHECK(w.id == "ab__launch");
    CHECK(w.component == "ab__widget");
    CHECK(w.icon == "tune");
    // Stored in tracks, half a cell each.
    CHECK(w.colspan == 2);
    CHECK(w.max_colspan == 4);
    CHECK(w.max_rowspan == 2);
    CHECK_FALSE(w.half_cells);
}

TEST_CASE("manifest widgets take half cells only when they opt in", "[plugin][manifest]") {
    auto r = with_widgets(R"({"id":"ab__l","name":"L","component":"ab__w","half_cells":true,
        "colspan":1.5,"max_colspan":3.5})");
    REQUIRE(r.manifest);
    REQUIRE(r.manifest->widgets.size() == 1);
    const auto& w = r.manifest->widgets[0];
    CHECK(w.half_cells);
    CHECK(w.colspan == 3);
    CHECK(w.max_colspan == 7);

    CHECK(has_error_containing(
        with_widgets(R"({"id":"ab__l","name":"L","component":"ab__w","colspan":1.5})"),
        "'colspan'"));
    CHECK(has_error_containing(
        with_widgets(
            R"({"id":"ab__l","name":"L","component":"ab__w","half_cells":true,"colspan":1.25})"),
        "'colspan'"));
    CHECK(has_error_containing(
        with_widgets(R"({"id":"ab__l","name":"L","component":"ab__w","half_cells":"yes"})"),
        "'half_cells'"));
}

TEST_CASE("manifest widgets reject bad names and spans", "[plugin][manifest]") {
    CHECK(has_error_containing(with_widgets(R"({"id":"launch","name":"L","component":"ab__w"})"),
                               "widgets[0]"));
    CHECK(has_error_containing(with_widgets(R"({"id":"ab__l","name":"L","component":"w"})"),
                               "'component'"));
    CHECK(has_error_containing(
        with_widgets(R"({"id":"ab__l","name":"L","component":"ab__w","colspan":9})"), "'colspan'"));
    CHECK(has_error_containing(
        with_widgets(
            R"({"id":"ab__l","name":"L","component":"ab__w","colspan":2,"max_colspan":1})"),
        "'max_colspan'"));
    CHECK(has_error_containing(with_widgets(R"({"id":"ab__l","name":"L","component":"ab__w"},
                                               {"id":"ab__l","name":"M","component":"ab__w"})"),
                               "duplicate widget id"));
    auto widgets = [](int n) {
        std::string out;
        for (int i = 0; i < n; ++i)
            out += std::string(i ? "," : "") + R"({"id":"ab__w)" + std::to_string(i) +
                   R"(","name":"W","component":"ab__c"})";
        return out;
    };
    auto most = with_widgets(widgets(32));
    REQUIRE(most.manifest);
    CHECK(most.manifest->widgets.size() == 32);
    auto over = with_widgets(widgets(33));
    CHECK_FALSE(over.manifest);
    CHECK(has_error_containing(over, "'widgets' may list at most 32"));
}

TEST_CASE("menu_items parse", "[plugin][manifest]") {
    auto r = with_menu_items(R"({"id":"calibrate","menu":"print_file","label":"Calibrate",
        "icon":"tune","handler":"calibrate","check":"can_calibrate"},
        {"id":"lookup","menu":"spool","label":"Look up","handler":"look-up_2"})");
    REQUIRE(r.errors.empty());
    REQUIRE(r.manifest);
    REQUIRE(r.manifest->menu_items.size() == 2);
    const auto& a = r.manifest->menu_items[0];
    CHECK(a.id == "calibrate");
    CHECK(a.menu == MenuKind::PrintFile);
    CHECK(a.label == "Calibrate");
    CHECK(a.icon == "tune");
    CHECK(a.handler == "calibrate");
    CHECK(a.check == "can_calibrate");
    const auto& b = r.manifest->menu_items[1];
    CHECK(b.menu == MenuKind::Spool);
    CHECK(b.handler == "look-up_2");
    CHECK(b.icon.empty());
    CHECK(b.check.empty());

    auto tile = with_menu_items(R"({"id":"t","menu":"tile","label":"T","handler":"h"},
        {"id":"s","menu":"filament_slot","label":"S","handler":"h"})",
                                /*widget=*/true);
    REQUIRE(tile.manifest);
    CHECK(tile.manifest->menu_items[0].menu == MenuKind::Tile);
    CHECK(tile.manifest->menu_items[1].menu == MenuKind::FilamentSlot);

    auto none = with_widgets(R"({"id":"ab__w","name":"W","component":"ab__c"})");
    REQUIRE(none.manifest);
    CHECK(none.manifest->menu_items.empty());
}

TEST_CASE("menu kinds round-trip through their names", "[plugin][manifest]") {
    for (auto k : {MenuKind::PrintFile, MenuKind::FilamentSlot, MenuKind::Spool, MenuKind::Tile})
        CHECK(parse_menu_kind(menu_kind_name(k)) == k);
    CHECK(std::string(menu_kind_name(MenuKind::FilamentSlot)) == "filament_slot");
    CHECK_FALSE(parse_menu_kind("toolbar"));
    CHECK_FALSE(parse_menu_kind(""));
}

TEST_CASE("menu_items errors", "[plugin][manifest]") {
    struct Case {
        const char* items;
        const char* needle;
    };
    const std::string long_label(kMaxMenuLabelBytes + 1, 'x');
    const std::string long_item =
        R"({"id":"a","menu":"spool","label":")" + long_label + R"(","handler":"h"})";
    const Case cases[] = {
        {R"({"menu":"spool","label":"L","handler":"h"})", "menu_items[0]: 'id'"},
        {R"({"id":"a","menu":"spool","handler":"h"})", "menu_items[0]: 'label'"},
        {R"({"id":"a","menu":"spool","label":"L"})", "menu_items[0]: 'handler'"},
        {R"({"id":"a","menu":"spool","label":"L","handler":"Bad Name"})",
         "menu_items[0]: 'handler'"},
        {R"({"id":"a","menu":"spool","label":"L","handler":"h","check":"can check"})",
         "menu_items[0]: 'check'"},
        {R"({"id":"a","menu":"toolbar","label":"L","handler":"h"})",
         "menu_items[0]: 'menu' must be one of print_file, filament_slot, spool, tile"},
        {R"({"id":"a","label":"L","handler":"h"})", "menu_items[0]: 'menu'"},
        {R"({"id":"a","menu":"spool","label":"L","handler":"h","icon":3})",
         "menu_items[0]: 'icon'"},
        {R"({"id":"a","menu":"spool","label":"L","handler":"h","icon":"no_such_icon"})",
         "menu_items[0]: 'icon' 'no_such_icon' is not an app icon name"},
        {long_item.c_str(), "menu_items[0]: 'label'"},
        {R"({"id":"a","menu":"tile","label":"L","handler":"h"})",
         "menu_items[0]: 'tile' items need a widget"},
        {R"("not an object")", "menu_items[0] must be an object"},
    };
    for (const auto& c : cases) {
        INFO(c.items);
        auto r = with_menu_items(c.items);
        CHECK_FALSE(r.manifest);
        CHECK(has_error_containing(r, c.needle));
    }

    auto dup = with_menu_items(R"({"id":"a","menu":"spool","label":"L","handler":"h"},
        {"id":"a","menu":"print_file","label":"M","handler":"h"})");
    CHECK_FALSE(dup.manifest);
    CHECK(has_error_containing(dup, "menu_items[1]: duplicate item id 'a'"));

    CHECK(with_menu_items(spool_items(kMaxMenuItemsPerMenu)).manifest);
    auto five = with_menu_items(spool_items(kMaxMenuItemsPerMenu + 1, "print_file"));
    CHECK_FALSE(five.manifest);
    CHECK(has_error_containing(five, "at most 4 items for print_file"));

    auto seventeen = parse_manifest(R"({"id":"ab","name":"n","version":"1","menu_items":[)" +
                                    spool_items(kMaxMenuItems + 1) + "]}");
    CHECK_FALSE(seventeen.manifest);
    CHECK(has_error_containing(seventeen, "'menu_items' may list at most 16"));

    auto not_array =
        parse_manifest(R"({"id":"ab","name":"n","version":"1","menu_items":{"id":"a"}})");
    CHECK_FALSE(not_array.manifest);
    CHECK(has_error_containing(not_array, "'menu_items' must be an array"));
}

TEST_CASE("local handler names", "[plugin][manifest]") {
    CHECK(is_valid_local_name("calibrate"));
    CHECK(is_valid_local_name("a-b_9"));
    CHECK(is_valid_local_name(std::string(48, 'a')));
    CHECK_FALSE(is_valid_local_name(std::string(49, 'a')));
    CHECK_FALSE(is_valid_local_name(""));
    CHECK_FALSE(is_valid_local_name("Bad"));
    CHECK_FALSE(is_valid_local_name("a b"));
}

namespace {
ManifestParse with_icons(const std::string& icons, const std::string& widgets = "") {
    return parse_manifest(R"({"id":"ab","name":"n","version":"1","icons":{)" + icons + "}" +
                          (widgets.empty() ? std::string() : R"(,"widgets":[)" + widgets + "]") +
                          "}");
}
} // namespace

TEST_CASE("manifest icons register plugin-local PNGs", "[plugin][manifest][icons]") {
    auto r = with_icons(R"("ab__oil":{"src":"icons/oil.png","fallback":"water"},
                           "ab__belt":{"src":"belt.png"})",
                        R"({"id":"ab__w","name":"W","component":"ab__c","icon":"ab__oil"})");
    REQUIRE(r.errors.empty());
    REQUIRE(r.manifest->icons.size() == 2);
    const auto& icons = r.manifest->icons;
    auto oil =
        std::find_if(icons.begin(), icons.end(), [](auto& d) { return d.name == "ab__oil"; });
    auto belt =
        std::find_if(icons.begin(), icons.end(), [](auto& d) { return d.name == "ab__belt"; });
    REQUIRE(oil != icons.end());
    REQUIRE(belt != icons.end());
    CHECK(oil->src == "icons/oil.png");
    CHECK(oil->fallback == "water");
    CHECK(belt->fallback == "puzzle_outline");
    CHECK(r.manifest->widgets[0].icon == "ab__oil");
}

TEST_CASE("manifest icons are validated", "[plugin][manifest][icons]") {
    struct Case {
        const char* icons;
        const char* error;
    };
    const Case cases[] = {
        {R"("cd__x":{"src":"x.png"})", "the name must be ab__<name>"},
        {R"("ab__Bad":{"src":"x.png"})", "the name must be ab__<name>"},
        {R"("ab__x":"x.png")", "must be an object"},
        {R"("ab__x":{})", "'src' must be a non-empty string"},
        {R"("ab__x":{"src":"../x.png"})", "'src' must be a .png path inside the plugin folder"},
        {R"("ab__x":{"src":"/etc/x.png"})", "'src' must be a .png path inside the plugin folder"},
        {R"("ab__x":{"src":"a//x.png"})", "'src' must be a .png path inside the plugin folder"},
        {R"("ab__x":{"src":"x.svg"})", "'src' must be a .png path inside the plugin folder"},
        {R"("ab__x":{"src":"x.png","fallback":"no_such_icon"})",
         "'fallback' 'no_such_icon' is not an app icon name"},
        {R"("ab__x":{"src":"x.png","fallback":7})", "'fallback' must be a string"},
    };
    for (const auto& c : cases) {
        INFO(c.icons);
        auto r = with_icons(c.icons);
        CHECK_FALSE(r.manifest);
        CHECK(has_error_containing(r, c.error));
    }
    CHECK(has_error_containing(parse_manifest(R"({"id":"ab","name":"n","version":"1","icons":[]})"),
                               "'icons' must be an object"));

    std::string many;
    for (size_t i = 0; i <= kMaxIconsPerPlugin; ++i)
        many +=
            std::string(i ? "," : "") + R"("ab__i)" + std::to_string(i) + R"(":{"src":"i.png"})";
    CHECK(has_error_containing(with_icons(many), "'icons' may list at most 16"));
}

TEST_CASE("a widget icon naming a plugin icon must be declared", "[plugin][manifest][icons]") {
    auto undeclared =
        with_icons(R"("ab__oil":{"src":"oil.png"})",
                   R"({"id":"ab__w","name":"W","component":"ab__c","icon":"ab__belt"})");
    CHECK(has_error_containing(undeclared, "'icon' 'ab__belt' is not declared in 'icons'"));
    auto foreign =
        with_widgets(R"({"id":"ab__w","name":"W","component":"ab__c","icon":"cd__oil"})");
    CHECK(has_error_containing(foreign, "'icon' 'cd__oil' is not declared in 'icons'"));
    CHECK(with_widgets(R"({"id":"ab__w","name":"W","component":"ab__c","icon":"water"})")
              .errors.empty());
}

#endif // HELIX_HAS_PLUGINS
