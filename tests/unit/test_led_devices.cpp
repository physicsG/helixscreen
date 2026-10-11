// SPDX-License-Identifier: GPL-3.0-or-later

#include "../helix_test_fixture.h"
#include "led/led_devices.h"

#include "../catch_amalgamated.hpp"

using namespace helix::led;

namespace {

LedStripInfo dev(const std::string& id, LedBackendType backend) {
    LedStripInfo s;
    s.id = id;
    s.name = id;
    s.backend = backend;
    s.supports_color = false;
    s.supports_white = false;
    return s;
}

const LedStripInfo SB = dev("neopixel sb_leds", LedBackendType::NATIVE);

} // namespace

TEST_CASE("resolve_chamber_light: every spelling matches", "[led][devices]") {
    CHECK(resolve_chamber_light({SB, dev("neopixel chamber_light", LedBackendType::NATIVE)},
                                "fb") == "neopixel chamber_light");
    CHECK(resolve_chamber_light({SB, dev("led chamber_LED", LedBackendType::NATIVE)}, "fb") ==
          "led chamber_LED");
    CHECK(resolve_chamber_light({SB, dev("neopixel case_light", LedBackendType::NATIVE)}, "fb") ==
          "neopixel case_light");
    CHECK(resolve_chamber_light({SB, dev("output_pin caselight", LedBackendType::OUTPUT_PIN)},
                                "fb") == "output_pin caselight");
}

TEST_CASE("resolve_chamber_light: a plural name is the same light", "[led][devices]") {
    const LedStripInfo corners = dev("neopixel Turtle_Corner_Indicators", LedBackendType::NATIVE);
    CHECK(resolve_chamber_light({corners, SB, dev("neopixel case_lights", LedBackendType::NATIVE)},
                                "fb") == "neopixel case_lights");
    CHECK(resolve_chamber_light({dev("neopixel chamber_lights", LedBackendType::NATIVE)}, "fb") ==
          "neopixel chamber_lights");
    CHECK(resolve_chamber_light({dev("led chamber_LEDs", LedBackendType::NATIVE)}, "fb") ==
          "led chamber_LEDs");
    CHECK(resolve_chamber_light({dev("output_pin caselights", LedBackendType::OUTPUT_PIN)}, "fb") ==
          "output_pin caselights");
}

TEST_CASE("resolve_chamber_light: case does not matter", "[led][devices]") {
    CHECK(resolve_chamber_light({dev("neopixel Chamber_Light", LedBackendType::NATIVE)}, "fb") ==
          "neopixel Chamber_Light");
    CHECK(resolve_chamber_light({dev("led CASELIGHT", LedBackendType::NATIVE)}, "fb") ==
          "led CASELIGHT");
}

TEST_CASE("resolve_chamber_light: spelling order is preference order", "[led][devices]") {
    const std::vector<LedStripInfo> both = {dev("led caselight", LedBackendType::NATIVE),
                                            dev("neopixel chamber_light", LedBackendType::NATIVE)};
    CHECK(resolve_chamber_light(both, "fb") == "neopixel chamber_light");
}

TEST_CASE("resolve_chamber_light: only whole object names match", "[led][devices]") {
    CHECK(resolve_chamber_light({dev("neopixel chamber_light_bar", LedBackendType::NATIVE),
                                 dev("neopixel my_caselight", LedBackendType::NATIVE)},
                                "fb") == "fb");
}

TEST_CASE("resolve_chamber_light: macros and WLED are not Klipper objects", "[led][devices]") {
    CHECK(resolve_chamber_light({dev("macro:chamber_light", LedBackendType::MACRO),
                                 dev("chamber_light", LedBackendType::WLED)},
                                "fb") == "fb");
}

TEST_CASE("resolve_chamber_light: fallback, and no devices at all", "[led][devices]") {
    CHECK(resolve_chamber_light({SB}, "neopixel sb_leds") == "neopixel sb_leds");
    CHECK(resolve_chamber_light({}, "").empty());
}

TEST_CASE("resolve_light_targets: each key shape", "[led][devices]") {
    const std::vector<std::string> sw = {"neopixel a", "neopixel b", "macro:Lamp"};
    CHECK(resolve_light_targets("", sw, "neopixel a") == std::vector<std::string>{"neopixel a"});
    CHECK(resolve_light_targets(LIGHT_BUTTON_ALL, sw, "neopixel a") == sw);
    CHECK(resolve_light_targets("macro:Lamp", sw, "neopixel a") ==
          std::vector<std::string>{"macro:Lamp"});
}

TEST_CASE("resolve_light_targets: an unknown id falls back to the chamber light",
          "[led][devices]") {
    const std::vector<std::string> sw = {"neopixel a"};
    CHECK(resolve_light_targets("neopixel gone", sw, "neopixel a") ==
          std::vector<std::string>{"neopixel a"});
}

TEST_CASE("resolve_light_targets: nothing to drive", "[led][devices]") {
    CHECK(resolve_light_targets("", {}, "").empty());
    CHECK(resolve_light_targets(LIGHT_BUTTON_ALL, {}, "").empty());
    CHECK(resolve_light_targets("neopixel gone", {}, "").empty());
}

TEST_CASE("union_light_targets: union in first-seen order, chamber when no buttons",
          "[led][devices]") {
    const std::vector<std::string> sw = {"neopixel a", "neopixel b", "neopixel c"};
    CHECK(union_light_targets({}, sw, "neopixel b") == std::vector<std::string>{"neopixel b"});
    CHECK(union_light_targets({"neopixel c", "", "neopixel c"}, sw, "neopixel b") ==
          std::vector<std::string>{"neopixel c", "neopixel b"});
    CHECK(union_light_targets({"", LIGHT_BUTTON_ALL}, sw, "neopixel b") ==
          std::vector<std::string>{"neopixel b", "neopixel a", "neopixel c"});
}

TEST_CASE("next_power_on: any on turns everything off", "[led][devices]") {
    CHECK_FALSE(next_power_on({PowerState::Off, PowerState::On}, false));
    CHECK_FALSE(next_power_on({PowerState::Unknown, PowerState::On}, false));
}

TEST_CASE("next_power_on: known off with none on turns on", "[led][devices]") {
    CHECK(next_power_on({PowerState::Off, PowerState::Unknown}, true));
    CHECK(next_power_on({PowerState::Off}, true));
}

TEST_CASE("next_power_on: unreadable state alternates on what was last sent", "[led][devices]") {
    CHECK(next_power_on({PowerState::Unknown}, false));
    CHECK_FALSE(next_power_on({PowerState::Unknown, PowerState::Unknown}, true));
    CHECK(next_power_on({}, false));
}

TEST_CASE("plan_selection_migration: one device", "[led][migration]") {
    const auto m = plan_selection_migration({"neopixel a"}, {"neopixel a", "neopixel b"});
    CHECK(m.light_button == "neopixel a");
    CHECK(m.auto_state_strips == std::vector<std::string>{"neopixel a"});
}

TEST_CASE("plan_selection_migration: every switchable device", "[led][migration]") {
    const auto m =
        plan_selection_migration({"neopixel b", "neopixel a"}, {"neopixel a", "neopixel b"});
    CHECK(m.light_button == LIGHT_BUTTON_ALL);
    CHECK(m.auto_state_strips == std::vector<std::string>{"neopixel b", "neopixel a"});
}

TEST_CASE("plan_selection_migration: an undiscovered extra id still means every device",
          "[led][migration]") {
    const auto m = plan_selection_migration({"neopixel a", "printer_led"}, {"neopixel a"});
    CHECK(m.light_button == LIGHT_BUTTON_ALL);
}

TEST_CASE("plan_selection_migration: anything else goes to auto-state only", "[led][migration]") {
    const auto m = plan_selection_migration({"neopixel a", "neopixel b"},
                                            {"neopixel a", "neopixel b", "neopixel c"});
    CHECK(m.light_button.empty());
    CHECK(m.auto_state_strips == std::vector<std::string>{"neopixel a", "neopixel b"});
}

TEST_CASE("plan_selection_migration: nothing selected", "[led][migration]") {
    CHECK(plan_selection_migration({}, {"neopixel a"}) == SelectionMigration{});
}

TEST_CASE("toggle_target: add, remove, and the last one stays", "[led][settings]") {
    CHECK(toggle_target({"a"}, "b") == std::vector<std::string>{"a", "b"});
    CHECK(toggle_target({"a", "b"}, "a") == std::vector<std::string>{"b"});
    CHECK(toggle_target({"a"}, "a") == std::vector<std::string>{"a"});
    CHECK(toggle_target({}, "a") == std::vector<std::string>{"a"});
}

TEST_CASE("migrate_color_presets", "[led][migration]") {
    const std::vector<uint32_t> fresh(std::begin(DEFAULT_COLOR_PRESETS),
                                      std::end(DEFAULT_COLOR_PRESETS));
    const std::vector<uint32_t> old(std::begin(PRE_1_1_DEFAULT_COLOR_PRESETS),
                                    std::end(PRE_1_1_DEFAULT_COLOR_PRESETS));
    CHECK(migrate_color_presets({}) == fresh);
    CHECK(migrate_color_presets(old) == fresh);
    auto reordered = old;
    std::swap(reordered[0], reordered[1]);
    CHECK(migrate_color_presets(reordered) == reordered);
    CHECK(migrate_color_presets({0x123456}) == std::vector<uint32_t>{0x123456});
    CHECK(fresh == std::vector<uint32_t>{0xFF4444, 0xFF6B35, 0x66BB6A, 0x00BCD4, 0x2962FF, 0x9C27B0,
                                         0xFF4081});
}

TEST_CASE("pick_overlay_focus: each entry point", "[led][overlay]") {
    const std::vector<std::string> d = {"neopixel a", "neopixel chamber_light", "macro:Party"};
    CHECK(pick_overlay_focus("macro:Party", "neopixel a", "neopixel chamber_light", d) ==
          "macro:Party");
    CHECK(pick_overlay_focus("", "neopixel a", "neopixel chamber_light", d) == "neopixel a");
    CHECK(pick_overlay_focus("", "", "neopixel chamber_light", d) == "neopixel chamber_light");
    CHECK(pick_overlay_focus("gone", "gone", "neopixel chamber_light", d) ==
          "neopixel chamber_light");
    CHECK(pick_overlay_focus("", "", "", d) == "neopixel a");
    CHECK(pick_overlay_focus("x", "y", "z", {}).empty());
}

TEST_CASE("device_display_name: Klipper object name prettified, macros as configured",
          "[led][devices]") {
    CHECK(device_display_name(dev("neopixel chamber_light", LedBackendType::NATIVE)) ==
          "Chamber Light");
    CHECK(device_display_name(dev("led caselight", LedBackendType::NATIVE)) == "Caselight");
    CHECK(device_display_name(dev("neopixel sb_leds", LedBackendType::NATIVE)) == "Sb LEDs");
    CHECK(device_display_name(dev("output_pin Enclosure_LEDs", LedBackendType::OUTPUT_PIN)) ==
          "Enclosure LEDs");
    CHECK(device_display_name(dev("printer_led", LedBackendType::WLED)) == "Printer LED");
    CHECK(device_display_name(dev("macro:lights", LedBackendType::MACRO)) == "lights");
    CHECK(device_display_name(dev("macro:Party Mode", LedBackendType::MACRO)) == "Party Mode");
}

TEST_CASE_METHOD(HelixTestFixture, "macro_device_note: what each macro type runs",
                 "[led][devices]") {
    LedMacroInfo m;
    m.type = MacroLedType::ON_OFF;
    m.on_macro = "LIGHTS_ON";
    m.off_macro = "LIGHTS_OFF";
    CHECK(macro_device_note(m) == "ON: LIGHTS_ON | OFF: LIGHTS_OFF");
    m.off_macro.clear();
    CHECK(macro_device_note(m) == "ON: LIGHTS_ON | OFF: —");

    m.type = MacroLedType::TOGGLE;
    m.toggle_macro = "LIGHT_TOGGLE";
    CHECK(macro_device_note(m) == "TOGGLE: LIGHT_TOGGLE");
    m.toggle_macro.clear();
    CHECK(macro_device_note(m) == "TOGGLE: —");

    m.type = MacroLedType::PRESET;
    CHECK(macro_device_note(m) == "No presets configured");
    m.presets = {"LED_PARTY"};
    CHECK(macro_device_note(m) == "1 preset");
    m.presets = {"LED_PARTY", "LED_RAINBOW"};
    CHECK(macro_device_note(m) == "2 presets");
}
