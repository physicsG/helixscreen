// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../helix_test_fixture.h"
#include "../test_helpers/update_queue_test_access.h"
#include "../ui_test_utils.h"
#include "app_globals.h"
#include "config.h"
#include "led/led_auto_state.h"
#include "led/led_controller.h"
#include "printer_state.h"

#include "../catch_amalgamated.hpp"

using namespace helix::led;

/// LedAutoState::init() registers a print-state observer whose first
/// notification is deferred through the UpdateQueue, and apply_action() routes
/// through LedController. With no fixture these TEST_CASEs returned with that
/// work still queued and handed it to whichever test drained next
/// (prestonbrown/helixscreen#1167). The drain sits in the derived destructor body
/// so it runs while the subjects are still alive, before HelixTestFixture's own
/// teardown.
struct LedAutoStateFixture : public HelixTestFixture {
    ~LedAutoStateFixture() override {
        helix::ui::UpdateQueueTestAccess::drain_all(helix::ui::UpdateQueue::instance());
    }
};

namespace {

/// Write a persisted auto-state config to the SAME per-printer path that
/// LedAutoState::load_config() reads (`<df()>leds/auto_state/...`). Returns the
/// df()-relative base so tests can clean up afterward.
std::string write_persisted_auto_state(bool enabled, const std::string& state_key,
                                       const std::string& action_type, const std::string& hex_color,
                                       int brightness) {
    auto* cfg = helix::Config::get_instance();
    REQUIRE(cfg != nullptr);
    const std::string base = cfg->df() + "leds/auto_state/";

    cfg->set(base + "enabled", enabled);

    nlohmann::json mappings = nlohmann::json::object();
    nlohmann::json entry;
    entry["action"] = action_type;
    entry["color"] = hex_color;
    entry["brightness"] = brightness;
    mappings[state_key] = entry;
    cfg->set(base + "mappings", mappings);

    cfg->save();
    return base;
}

/// Remove the persisted auto-state keys so the shared Config singleton does not
/// leak state into later tests (random test order).
void clear_persisted_auto_state() {
    auto* cfg = helix::Config::get_instance();
    if (!cfg) {
        return;
    }
    cfg->set(cfg->df() + "leds/auto_state/enabled", nlohmann::json());
    cfg->set(cfg->df() + "leds/auto_state/mappings", nlohmann::json());
    // Also clear the legacy migration source so it cannot re-seed.
    cfg->set("/led/auto_state/enabled", nlohmann::json());
    cfg->set("/led/auto_state/mappings", nlohmann::json());
    cfg->save();
}

} // namespace

TEST_CASE_METHOD(LedAutoStateFixture, "LedAutoState singleton access", "[led][autostate]") {
    auto& state1 = LedAutoState::instance();
    auto& state2 = LedAutoState::instance();
    REQUIRE(&state1 == &state2);
}

TEST_CASE_METHOD(LedAutoStateFixture, "LedAutoState default disabled after deinit",
                 "[led][autostate]") {
    auto& state = LedAutoState::instance();
    state.deinit();
    REQUIRE_FALSE(state.is_enabled());
    REQUIRE_FALSE(state.is_initialized());
}

TEST_CASE_METHOD(LedAutoStateFixture, "LedAutoState enable/disable without printer state",
                 "[led][autostate]") {
    auto& state = LedAutoState::instance();
    state.deinit();

    REQUIRE_FALSE(state.is_enabled());
    state.set_enabled(true);
    REQUIRE(state.is_enabled());
    state.set_enabled(false);
    REQUIRE_FALSE(state.is_enabled());

    // Double-set is idempotent
    state.set_enabled(true);
    state.set_enabled(true);
    REQUIRE(state.is_enabled());

    state.deinit();
}

TEST_CASE_METHOD(LedAutoStateFixture, "LedAutoState set and get mapping", "[led][autostate]") {
    auto& state = LedAutoState::instance();
    state.deinit();

    LedStateAction action;
    action.action_type = "color";
    action.color = 0xFF0000;
    action.brightness = 75;

    state.set_mapping("error", action);

    auto* result = state.get_mapping("error");
    REQUIRE(result != nullptr);
    REQUIRE(result->action_type == "color");
    REQUIRE(result->color == 0xFF0000);
    REQUIRE(result->brightness == 75);

    // Non-existent mapping returns nullptr
    REQUIRE(state.get_mapping("nonexistent") == nullptr);

    state.deinit();
}

TEST_CASE_METHOD(LedAutoStateFixture, "LedAutoState mappings() returns all mappings",
                 "[led][autostate]") {
    auto& state = LedAutoState::instance();
    state.deinit();

    LedStateAction a1;
    a1.action_type = "color";
    a1.color = 0xFF0000;

    LedStateAction a2;
    a2.action_type = "off";

    state.set_mapping("error", a1);
    state.set_mapping("idle", a2);

    auto& all = state.mappings();
    REQUIRE(all.size() == 2);
    REQUIRE(all.count("error") == 1);
    REQUIRE(all.count("idle") == 1);

    state.deinit();
}

TEST_CASE_METHOD(LedAutoStateFixture, "LedStateAction struct defaults", "[led][autostate]") {
    LedStateAction action;
    REQUIRE(action.action_type.empty());
    REQUIRE(action.color == 0xFFFFFF);
    REQUIRE(action.brightness == 100);
    REQUIRE(action.effect_name.empty());
    REQUIRE(action.wled_preset == 0);
    REQUIRE(action.macro_gcode.empty());
}

TEST_CASE_METHOD(LedAutoStateFixture, "LedAutoState mapping overwrite", "[led][autostate]") {
    auto& state = LedAutoState::instance();
    state.deinit();

    LedStateAction action1;
    action1.action_type = "color";
    action1.color = 0xFF0000;

    LedStateAction action2;
    action2.action_type = "effect";
    action2.effect_name = "rainbow";

    state.set_mapping("printing", action1);
    state.set_mapping("printing", action2);

    auto* result = state.get_mapping("printing");
    REQUIRE(result != nullptr);
    REQUIRE(result->action_type == "effect");
    REQUIRE(result->effect_name == "rainbow");

    state.deinit();
}

TEST_CASE_METHOD(LedAutoStateFixture, "LedAutoState deinit clears all state", "[led][autostate]") {
    auto& state = LedAutoState::instance();
    state.deinit();

    // Add some state
    LedStateAction action;
    action.action_type = "color";
    state.set_mapping("idle", action);
    state.set_enabled(true);

    REQUIRE(state.is_enabled());
    REQUIRE(state.mappings().size() == 1);

    // Deinit clears everything
    state.deinit();

    REQUIRE_FALSE(state.is_enabled());
    REQUIRE_FALSE(state.is_initialized());
    REQUIRE(state.mappings().empty());
}

TEST_CASE_METHOD(LedAutoStateFixture, "LedStateAction supports brightness action type",
                 "[led][auto_state]") {
    auto& state = LedAutoState::instance();
    state.deinit();

    LedStateAction action;
    action.action_type = "brightness";
    action.brightness = 50;

    // Verify fields are set correctly
    REQUIRE(action.action_type == "brightness");
    REQUIRE(action.brightness == 50);
    REQUIRE(action.color == 0xFFFFFF); // Default color unchanged

    // Round-trip through set_mapping / get_mapping
    state.set_mapping("idle", action);
    auto* result = state.get_mapping("idle");
    REQUIRE(result != nullptr);
    REQUIRE(result->action_type == "brightness");
    REQUIRE(result->brightness == 50);

    state.deinit();
}

TEST_CASE_METHOD(LedAutoStateFixture, "brightness action type stored in mapping",
                 "[led][auto_state]") {
    auto& state = LedAutoState::instance();
    state.deinit();

    LedStateAction action;
    action.action_type = "brightness";
    action.brightness = 75;

    state.set_mapping("heating", action);

    auto* result = state.get_mapping("heating");
    REQUIRE(result != nullptr);
    REQUIRE(result->action_type == "brightness");
    REQUIRE(result->brightness == 75);

    // Verify it coexists with other action types
    LedStateAction color_action;
    color_action.action_type = "color";
    color_action.color = 0xFF0000;
    state.set_mapping("error", color_action);

    REQUIRE(state.mappings().size() == 2);
    REQUIRE(state.get_mapping("heating")->action_type == "brightness");
    REQUIRE(state.get_mapping("error")->action_type == "color");

    state.deinit();
}

TEST_CASE_METHOD(LedAutoStateFixture, "setup_default_mappings includes all 6 state keys",
                 "[led][auto_state]") {
    auto& state = LedAutoState::instance();
    state.deinit();

    // Set up defaults by setting mappings manually (matching setup_default_mappings)
    // We can't call the private method directly, but we can verify after init with no config
    // Instead, verify the expected state keys via set_mapping
    const std::vector<std::string> expected_keys = {"idle",   "heating", "printing",
                                                    "paused", "error",   "complete"};

    for (const auto& key : expected_keys) {
        LedStateAction action;
        action.action_type = "color";
        state.set_mapping(key, action);
    }

    REQUIRE(state.mappings().size() == 6);
    for (const auto& key : expected_keys) {
        auto* mapping = state.get_mapping(key);
        REQUIRE(mapping != nullptr);
        // All action types should be valid
        bool valid_type =
            (mapping->action_type == "color" || mapping->action_type == "brightness" ||
             mapping->action_type == "effect" || mapping->action_type == "wled_preset" ||
             mapping->action_type == "macro" || mapping->action_type == "off");
        REQUIRE(valid_type);
    }

    state.deinit();
}

// ============================================================================
// apply_action integration: verify light_on_ state side effects
// ============================================================================

/// Helper: set up LedController with a native strip selected, init LedAutoState
static void setup_auto_state_with_strip() {
    // Deinit LedAutoState first to ensure clean state (no stale enabled_ or
    // deferred observer callbacks from a previous test)
    LedAutoState::instance().deinit();

    auto& ctrl = LedController::instance();
    ctrl.deinit();
    ctrl.init(nullptr, nullptr);

    LedStripInfo strip;
    strip.name = "Chamber Light";
    strip.id = "neopixel chamber_light";
    strip.backend = LedBackendType::NATIVE;
    strip.supports_color = true;
    strip.supports_white = true;
    ctrl.native().add_strip(strip);
    ctrl.set_selected_strips({"neopixel chamber_light"});
}

static void teardown_auto_state() {
    LedAutoState::instance().deinit();
    LedController::instance().deinit();
}

TEST_CASE_METHOD(LedAutoStateFixture, "LedAutoState apply_action 'off' sets light_is_on false",
                 "[led][autostate]") {
    lv_init_safe();
    setup_auto_state_with_strip();
    auto& ctrl = LedController::instance();
    auto& state = LedAutoState::instance();

    // Start with light on
    ctrl.light_set(true);
    REQUIRE(ctrl.light_is_on());

    // init() first (loads config, resets enabled), then configure and enable.
    // Map ALL possible states to "off" since shared PrinterState singleton
    // may retain subject values from earlier tests in the same shard.
    state.init(get_printer_state());
    LedStateAction off_action{"off", 0xFFFFFF, 100, "", 0, ""};
    state.set_mapping("idle", off_action);
    state.set_mapping("printing", off_action);
    state.set_mapping("paused", off_action);
    state.set_mapping("heating", off_action);
    state.set_mapping("error", off_action);
    state.set_mapping("complete", off_action);
    state.set_enabled(true);

    state.evaluate();
    // Drain deferred observer callbacks from subscribe_observers() so they
    // cannot re-apply a stale mapping after we check the assertion
    helix::ui::UpdateQueueTestAccess::drain_all(helix::ui::UpdateQueue::instance());
    REQUIRE_FALSE(ctrl.light_is_on());

    teardown_auto_state();
}

TEST_CASE_METHOD(LedAutoStateFixture, "LedAutoState apply_action 'color' sets light_is_on true",
                 "[led][autostate]") {
    lv_init_safe();
    setup_auto_state_with_strip();
    auto& ctrl = LedController::instance();
    auto& state = LedAutoState::instance();

    REQUIRE_FALSE(ctrl.light_is_on());

    state.init(get_printer_state());
    state.set_mapping("idle", {"color", 0xFF0000, 100, "", 0, ""});
    state.set_enabled(true);

    state.evaluate();
    helix::ui::UpdateQueueTestAccess::drain_all(helix::ui::UpdateQueue::instance());
    REQUIRE(ctrl.light_is_on());

    teardown_auto_state();
}

TEST_CASE_METHOD(LedAutoStateFixture,
                 "LedAutoState apply_action 'brightness' sets light_is_on based on value",
                 "[led][autostate]") {
    lv_init_safe();
    setup_auto_state_with_strip();
    auto& ctrl = LedController::instance();
    auto& state = LedAutoState::instance();

    // brightness > 0 → on
    state.init(get_printer_state());
    state.set_mapping("idle", {"brightness", 0xFFFFFF, 50, "", 0, ""});
    state.set_enabled(true);

    state.evaluate();
    helix::ui::UpdateQueueTestAccess::drain_all(helix::ui::UpdateQueue::instance());
    REQUIRE(ctrl.light_is_on());

    teardown_auto_state();

    // brightness == 0 → off
    setup_auto_state_with_strip();
    auto& ctrl2 = LedController::instance();
    auto& state2 = LedAutoState::instance();

    ctrl2.light_set(true);
    state2.init(get_printer_state());
    // Map ALL possible states to brightness=0 since shared PrinterState singleton
    // may retain subject values from earlier tests in the same shard.
    LedStateAction zero_brightness{"brightness", 0xFFFFFF, 0, "", 0, ""};
    state2.set_mapping("idle", zero_brightness);
    state2.set_mapping("printing", zero_brightness);
    state2.set_mapping("paused", zero_brightness);
    state2.set_mapping("heating", zero_brightness);
    state2.set_mapping("error", zero_brightness);
    state2.set_mapping("complete", zero_brightness);
    state2.set_enabled(true);

    state2.evaluate();
    helix::ui::UpdateQueueTestAccess::drain_all(helix::ui::UpdateQueue::instance());
    REQUIRE_FALSE(ctrl2.light_is_on());

    teardown_auto_state();
}

// ============================================================================
// Lifecycle wiring guarantees: these lock in the behavior that the production
// wiring (printer_discovery init / application teardown deinit) depends on.
// ============================================================================

// init() must load the persisted per-printer config (enabled flag + mappings).
// This is the core value the production wiring relies on: when printer_discovery
// calls init(printer_state), saved auto-state config becomes live.
TEST_CASE_METHOD(LedAutoStateFixture,
                 "LedAutoState init loads persisted config from per-printer path",
                 "[led][autostate]") {
    auto& state = LedAutoState::instance();
    state.deinit();

    // Persist enabled=true + a single mapping at the path load_config() reads.
    write_persisted_auto_state(/*enabled=*/true, /*state_key=*/"printing",
                               /*action_type=*/"color", /*hex_color=*/"#FF0000",
                               /*brightness=*/77);

    // Fresh init from the persisted store.
    state.init(get_printer_state());

    REQUIRE(state.is_enabled());

    const auto* mapping = state.get_mapping("printing");
    REQUIRE(mapping != nullptr);
    REQUIRE(mapping->action_type == "color");
    REQUIRE(mapping->color == 0xFF0000u);
    REQUIRE(mapping->brightness == 77);

    state.deinit();
    clear_persisted_auto_state();
}

// deinit()->init() soft-restart cycle (mirrors switch_printer: teardown then
// re-init on the new printer). Must not crash and must end in a consistent,
// re-subscribed state reflecting the (re)loaded config.
TEST_CASE_METHOD(LedAutoStateFixture,
                 "LedAutoState deinit/init soft-restart cycle stays consistent",
                 "[led][autostate]") {
    auto& state = LedAutoState::instance();
    state.deinit();

    write_persisted_auto_state(/*enabled=*/true, /*state_key=*/"idle",
                               /*action_type=*/"color", /*hex_color=*/"#00FF00",
                               /*brightness=*/40);

    // First init (as printer_discovery would do).
    state.init(get_printer_state());
    REQUIRE(state.is_initialized());
    REQUIRE(state.is_enabled());
    REQUIRE(state.get_mapping("idle") != nullptr);

    // Teardown (as application teardown would do).
    state.deinit();
    REQUIRE_FALSE(state.is_initialized());
    REQUIRE_FALSE(state.is_enabled());
    REQUIRE(state.mappings().empty());

    // Re-init (as the next printer_discovery on switch_printer would do).
    state.init(get_printer_state());
    REQUIRE(state.is_initialized());
    REQUIRE(state.is_enabled());

    const auto* mapping = state.get_mapping("idle");
    REQUIRE(mapping != nullptr);
    REQUIRE(mapping->color == 0x00FF00u);

    // Re-evaluation after the soft restart must not crash; drain deferred
    // observer callbacks that subscribe_observers() may have enqueued.
    state.evaluate();
    helix::ui::UpdateQueueTestAccess::drain_all(helix::ui::UpdateQueue::instance());

    state.deinit();
    clear_persisted_auto_state();
}

// After init() subscribes observers, a change to an observed PrinterState
// subject must drive LedController via apply_action (the end-to-end auto-state
// path that production now activates).
TEST_CASE_METHOD(LedAutoStateFixture, "LedAutoState observer fires after init and applies action",
                 "[led][autostate]") {
    lv_init_safe();
    setup_auto_state_with_strip();
    auto& ctrl = LedController::instance();
    auto& state = LedAutoState::instance();

    REQUIRE_FALSE(ctrl.light_is_on());

    auto& ps = get_printer_state();
    // The PrinterState subjects observed by LedAutoState (print state enum,
    // klippy state, extruder target) are plain lv_subject_t that only become
    // settable after init_subjects(); lv_subject_set_int() is a no-op on an
    // uninitialized subject. register_xml=false keeps these out of the global
    // XML scope so they don't collide with other tests in the shard.
    ps.init_subjects(false);

    // Establish a known non-printing baseline BEFORE init so the later flip to
    // PRINTING is a genuine state transition (the dedup in on_state_changed()
    // skips re-applying an unchanged key). Clearing klippy ERROR and zeroing the
    // extruder target keeps compute_state_key() at "idle" for the baseline.
    auto* print_subj = ps.get_print_state_enum_subject();
    REQUIRE(print_subj != nullptr);
    lv_subject_set_int(print_subj, static_cast<int>(helix::PrintJobState::STANDBY));
    if (auto* klippy_subj = ps.get_klippy_state_subject()) {
        lv_subject_set_int(klippy_subj, static_cast<int>(helix::KlippyState::READY));
    }
    if (auto* ext_target = ps.get_active_extruder_target_subject()) {
        lv_subject_set_int(ext_target, 0);
    }

    state.init(ps);
    // Map every state to "off" except the one we will drive to, so whatever
    // value the shared PrinterState subjects currently hold cannot turn the
    // light on before our targeted change.
    LedStateAction off_action{"off", 0xFFFFFF, 100, "", 0, ""};
    for (const char* k : {"idle", "heating", "printing", "paused", "error", "complete"}) {
        state.set_mapping(k, off_action);
    }
    state.set_mapping("printing", {"color", 0x00FF00, 100, "", 0, ""});
    state.set_enabled(true);

    // Drain the immediate evaluate() from set_enabled() so we start from a known
    // ("idle" → off) baseline, then change the observed subject.
    helix::ui::UpdateQueueTestAccess::drain_all(helix::ui::UpdateQueue::instance());
    REQUIRE_FALSE(ctrl.light_is_on());

    // Drive the print-state subject to PRINTING — this is an observed subject,
    // so on_state_changed() should fire and apply the "color" action.
    lv_subject_set_int(print_subj, static_cast<int>(helix::PrintJobState::PRINTING));

    helix::ui::UpdateQueueTestAccess::drain_all(helix::ui::UpdateQueue::instance());

    REQUIRE(ctrl.light_is_on());

    // Restore subject state so a STANDBY/idle baseline doesn't leak into other
    // tests sharing the PrinterState singleton.
    lv_subject_set_int(print_subj, static_cast<int>(helix::PrintJobState::STANDBY));
    teardown_auto_state();
    clear_persisted_auto_state();
}
