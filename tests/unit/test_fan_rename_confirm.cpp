// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_fan_rename_confirm.cpp
 * @brief FanSettingsOverlay rename confirmation: input resolution and revert
 *
 * confirm_rename() has to read the textarea belonging to the modal the overlay
 * itself opened. Modals are parented to the active screen, so any other widget
 * carrying the same name is reachable from the same root, and an empty name is
 * a meaningful value that reverts the fan to its auto-generated name.
 */

#include "ui_modal.h"
#include "ui_settings_fans.h"

#include "../lvgl_ui_test_fixture.h"
#include "../test_helpers/log_capture.h"
#include "app_globals.h"
#include "config.h"
#include "printer_state.h"

#include <spdlog/spdlog.h>

#include <lvgl.h>
#include <memory>
#include <string>
#include <vector>

#include "../catch_amalgamated.hpp"
#include "hv/json.hpp"

namespace {

constexpr const char* kFanObject = "temperature_fan soc_fan";
constexpr const char* kInputName = "fan_rename_new_name_input";

std::string fan_name_key() {
    return helix::Config::get_instance()->df() + "fans/names/" + kFanObject;
}

std::string persisted_fan_name() {
    return helix::Config::get_instance()->get<std::string>(fan_name_key(), "<unset>");
}

/// Every persisted fan name, so a test can assert that a bail-out wrote no name
/// at all rather than only that one particular key survived.
nlohmann::json persisted_fan_names() {
    const std::string key = helix::Config::get_instance()->df() + "fans/names";
    return helix::Config::get_instance()->get<nlohmann::json>(key, nlohmann::json::object());
}

/// Returns the shared Config singleton's fan-name entry to its prior value, so
/// these tests cannot leak a custom name into any test that runs after them.
class ScopedFanNameKey {
  public:
    ScopedFanNameKey()
        : key_(fan_name_key()),
          original_(helix::Config::get_instance()->get<std::string>(key_, "")) {}

    ~ScopedFanNameKey() {
        helix::Config::get_instance()->set<std::string>(key_, original_);
    }

    ScopedFanNameKey(const ScopedFanNameKey&) = delete;
    ScopedFanNameKey& operator=(const ScopedFanNameKey&) = delete;

    void set(const std::string& value) const {
        helix::Config::get_instance()->set<std::string>(key_, value);
    }

  private:
    std::string key_;
    std::string original_;
};

/// The textarea inside whichever modal is currently on top of the stack.
lv_obj_t* live_modal_input() {
    lv_obj_t* top = helix::ui::modal_get_top();
    return top != nullptr ? lv_obj_find_by_name(top, kInputName) : nullptr;
}

} // namespace

TEST_CASE_METHOD(LVGLUITestFixture, "fan rename confirm reads the input of the modal it opened",
                 "[fans][rename][modal]") {
    ScopedFanNameKey name_key;
    name_key.set("");

    auto& overlay = helix::settings::get_fan_settings_overlay();
    overlay.register_callbacks();
    overlay.cancel_rename(); // no pending rename carried in from another test

    get_printer_state().fan_state().init_fans({kFanObject});

    // A textarea carrying the modal's input name, placed on the active screen
    // ahead of the modal. lv_obj_find_by_name walks depth-first from its root
    // and returns the first match, so a whole-screen search resolves to this
    // one rather than to the modal's own input.
    lv_obj_t* decoy = lv_textarea_create(lv_screen_active());
    lv_obj_set_name(decoy, kInputName);
    lv_textarea_set_text(decoy, "Decoy Fan Name");

    overlay.handle_fan_rename(kFanObject, "Soc Fan");

    lv_obj_t* input = live_modal_input();
    REQUIRE(input != nullptr);
    REQUIRE(input != decoy);

    lv_textarea_set_text(input, "Quiet SoC Fan");
    overlay.confirm_rename();

    REQUIRE(persisted_fan_name() == "Quiet SoC Fan");

    process_lvgl(100);
}

TEST_CASE_METHOD(LVGLUITestFixture, "clearing the fan rename input reverts the display name",
                 "[fans][rename][modal]") {
    ScopedFanNameKey name_key;
    name_key.set("");

    auto& overlay = helix::settings::get_fan_settings_overlay();
    overlay.register_callbacks();
    overlay.cancel_rename();

    // With no custom name persisted, init_fans derives the name the revert path
    // has to restore. Reading it here beats hardcoding a spelling this test does
    // not own.
    get_printer_state().fan_state().init_fans({kFanObject});
    REQUIRE(get_printer_state().fan_state().get_fans().size() == 1);
    const std::string auto_name = get_printer_state().fan_state().get_fans()[0].display_name;
    REQUIRE_FALSE(auto_name.empty());

    overlay.handle_fan_rename(kFanObject, auto_name);
    lv_obj_t* input = live_modal_input();
    REQUIRE(input != nullptr);
    lv_textarea_set_text(input, "Custom SoC Fan");
    overlay.confirm_rename();

    REQUIRE(persisted_fan_name() == "Custom SoC Fan");
    REQUIRE(get_printer_state().fan_state().get_fans()[0].display_name == "Custom SoC Fan");

    // Let the first modal's deferred delete run so the revert below is about the
    // empty name and not about which modal a lookup found.
    process_lvgl(100);

    overlay.handle_fan_rename(kFanObject, "Custom SoC Fan");
    lv_obj_t* revert_input = live_modal_input();
    REQUIRE(revert_input != nullptr);
    lv_textarea_set_text(revert_input, "");
    overlay.confirm_rename();

    // The display name is the honest observable for the revert: rename_fan
    // recomputes it from the role/auto-name rules, while the config entry only
    // records that no custom name is set.
    REQUIRE(get_printer_state().fan_state().get_fans()[0].display_name == auto_name);
    REQUIRE(persisted_fan_name().empty());

    process_lvgl(100);
}

TEST_CASE_METHOD(LVGLUITestFixture, "fan rename confirm with no pending fan persists nothing",
                 "[fans][rename][modal]") {
    ScopedFanNameKey name_key;
    name_key.set("Untouched Fan Name");

    auto& overlay = helix::settings::get_fan_settings_overlay();
    overlay.register_callbacks();
    overlay.cancel_rename(); // clears any pending object

    get_printer_state().fan_state().init_fans({kFanObject});

    // An input is reachable on the active screen, so a confirm that ignored the
    // pending-object guard would have a name to write.
    lv_obj_t* stray = lv_textarea_create(lv_screen_active());
    lv_obj_set_name(stray, kInputName);
    lv_textarea_set_text(stray, "Should Not Persist");

    const nlohmann::json before = persisted_fan_names();

    overlay.confirm_rename();

    REQUIRE(persisted_fan_name() == "Untouched Fan Name");
    // No name key anywhere gains a value: a confirm that ran the rename with an
    // empty pending object would write one under the empty object name.
    REQUIRE(persisted_fan_names() == before);

    process_lvgl(100);
}

TEST_CASE_METHOD(LVGLUITestFixture, "fan rename bail-outs name themselves in the log",
                 "[fans][rename][modal]") {
    ScopedFanNameKey name_key;
    name_key.set("");

    auto& overlay = helix::settings::get_fan_settings_overlay();
    overlay.register_callbacks();
    overlay.cancel_rename();

    get_printer_state().fan_state().init_fans({kFanObject});

    SECTION("a confirm with nothing pending is distinguishable from a rename") {
        helix::LogCapture log;
        overlay.confirm_rename();
        REQUIRE(log.has_line_with({"no fan pending"}));
    }

    SECTION("a missing input names the fan it left alone") {
        overlay.handle_fan_rename(kFanObject, "Soc Fan");
        lv_obj_t* input = live_modal_input();
        REQUIRE(input != nullptr);

        // With the only input gone, every lookup misses and the confirm has no
        // name to apply.
        lv_obj_delete(input);

        helix::LogCapture log;
        overlay.confirm_rename();
        REQUIRE(log.has_line_with({"Rename input not found", kFanObject}));
    }

    SECTION("a cancel is distinguishable from a failed confirm") {
        overlay.handle_fan_rename(kFanObject, "Soc Fan");

        helix::LogCapture log;
        overlay.cancel_rename();
        REQUIRE(log.has_line_with({"Rename modal dismissed", kFanObject}));
    }

    process_lvgl(100);
}

TEST_CASE_METHOD(LVGLUITestFixture, "fan rename logs the persist step and unknown objects",
                 "[fans][rename][modal]") {
    ScopedFanNameKey name_key;
    name_key.set("");

    auto& overlay = helix::settings::get_fan_settings_overlay();
    overlay.register_callbacks();
    overlay.cancel_rename();

    get_printer_state().fan_state().init_fans({kFanObject});

    SECTION("a rename that reaches config says so independently of the fan list") {
        overlay.handle_fan_rename(kFanObject, "Soc Fan");
        lv_obj_t* input = live_modal_input();
        REQUIRE(input != nullptr);
        lv_textarea_set_text(input, "Logged SoC Fan");

        helix::LogCapture log;
        overlay.confirm_rename();
        REQUIRE(log.has_line_with({"Persisted name", "Logged SoC Fan", kFanObject}));
    }

    SECTION("renaming an object the printer never reported is not silent") {
        constexpr const char* kGhost = "temperature_fan ghost_fan";
        const std::string ghost_key = helix::Config::get_instance()->df() + "fans/names/" + kGhost;
        const std::string ghost_orig =
            helix::Config::get_instance()->get<std::string>(ghost_key, "");

        overlay.handle_fan_rename(kGhost, "Ghost");
        lv_obj_t* input = live_modal_input();
        REQUIRE(input != nullptr);
        lv_textarea_set_text(input, "Ghost Fan");

        helix::LogCapture log;
        overlay.confirm_rename();
        REQUIRE(log.has_line_with({"not a discovered fan", kGhost}));

        helix::Config::get_instance()->set<std::string>(ghost_key, ghost_orig);
    }

    process_lvgl(100);
}
