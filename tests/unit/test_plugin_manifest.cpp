// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#if HELIX_HAS_PLUGINS

#include "plugin_manifest.h"
#include "plugin_permissions.h"

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
    std::string nine;
    for (int i = 0; i < 9; ++i)
        nine += std::string(i ? "," : "") + R"({"id":"ab__w)" + std::to_string(i) +
                R"(","name":"W","component":"ab__c"})";
    CHECK(has_error_containing(with_widgets(nine), "at most 8"));
}

#endif // HELIX_HAS_PLUGINS
