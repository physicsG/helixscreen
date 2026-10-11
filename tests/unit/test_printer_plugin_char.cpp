// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_printer_plugin_char.cpp
 * @brief Characterization tests for PrinterState plugin status domain
 *
 * These tests capture the CURRENT behavior of plugin-related subjects
 * in PrinterState before extraction to a dedicated PrinterPluginStatusState class.
 *
 * Plugin status subjects:
 * - helix_plugin_installed_ (int, tri-state: -1=unknown, 0=not installed, 1=installed)
 *
 * Update mechanisms:
 * - set_helix_plugin_installed(bool) - async update via the UpdateQueue
 *
 * Query methods:
 * - service_has_helix_plugin() - returns true only when value is 1
 *
 * Key behaviors:
 * - helix_plugin_installed_ is tri-state: -1 (unknown) is the initial value
 * - Unknown state (-1) is treated as false for boolean queries
 * - Updates trigger update_gcode_modification_visibility() which refreshes the
 *   aggregate has_any_preprint_options subject (per-op can_show_* subjects
 *   were retired — see PrinterCompositeVisibilityState).
 */

#include "../test_helpers/printer_state_test_access.h"
#include "../ui_test_utils.h"
#include "app_globals.h"
#include "printer_state.h"

#include "../catch_amalgamated.hpp"

using namespace helix;
using namespace helix::ui;
// Helper to get subject by XML name (requires init_subjects(true))
static lv_subject_t* get_subject_by_name(const char* name) {
    return lv_xml_get_subject(NULL, name);
}

// ============================================================================
// Initial Value Tests - Document tri-state initialization behavior
// ============================================================================

TEST_CASE("Plugin status characterization: initial values after init",
          "[characterization][plugin][init]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(true); // Need XML registration to lookup by name

    SECTION("helix_plugin_installed initializes to -1 (unknown)") {
        lv_subject_t* subject = get_subject_by_name("helix_plugin_installed");
        REQUIRE(subject != nullptr);
        REQUIRE(lv_subject_get_int(subject) == -1);
    }
}

TEST_CASE("Plugin status characterization: initial query methods return false for unknown state",
          "[characterization][plugin][init]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(false);

    SECTION("service_has_helix_plugin returns false when unknown (-1)") {
        REQUIRE(state.plugin_status_state().service_has_helix_plugin() == false);
    }
}

// ============================================================================
// set_helix_plugin_installed Tests - Verify plugin detection updates
// ============================================================================

TEST_CASE("Plugin status characterization: set_helix_plugin_installed behavior",
          "[characterization][plugin][setter]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(true);

    SECTION("set_helix_plugin_installed(true) sets subject to 1") {
        state.set_helix_plugin_installed(true);
        UpdateQueueTestAccess::drain(helix::ui::UpdateQueue::instance());

        lv_subject_t* subject = get_subject_by_name("helix_plugin_installed");
        REQUIRE(lv_subject_get_int(subject) == 1);
    }

    SECTION("set_helix_plugin_installed(false) sets subject to 0") {
        // First set to true
        state.set_helix_plugin_installed(true);
        UpdateQueueTestAccess::drain(helix::ui::UpdateQueue::instance());

        // Then set to false
        state.set_helix_plugin_installed(false);
        UpdateQueueTestAccess::drain(helix::ui::UpdateQueue::instance());

        lv_subject_t* subject = get_subject_by_name("helix_plugin_installed");
        REQUIRE(lv_subject_get_int(subject) == 0);
    }

    SECTION("service_has_helix_plugin returns true after set_helix_plugin_installed(true)") {
        state.set_helix_plugin_installed(true);
        UpdateQueueTestAccess::drain(helix::ui::UpdateQueue::instance());

        REQUIRE(state.plugin_status_state().service_has_helix_plugin() == true);
    }

    SECTION("service_has_helix_plugin returns false after set_helix_plugin_installed(false)") {
        state.set_helix_plugin_installed(false);
        UpdateQueueTestAccess::drain(helix::ui::UpdateQueue::instance());

        REQUIRE(state.plugin_status_state().service_has_helix_plugin() == false);
    }
}

// ============================================================================
// Tri-state Semantics Tests - Verify -1/0/1 distinction is maintained
// ============================================================================

TEST_CASE("Plugin status characterization: tri-state semantics",
          "[characterization][plugin][tristate]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(false);

    SECTION("helix_plugin_installed: unknown (-1) vs not installed (0) are distinct") {
        lv_subject_t* subject = state.plugin_status_state().get_helix_plugin_installed_subject();

        // Initially unknown
        REQUIRE(lv_subject_get_int(subject) == -1);
        REQUIRE(state.plugin_status_state().service_has_helix_plugin() == false);

        // Set to not installed
        state.set_helix_plugin_installed(false);
        UpdateQueueTestAccess::drain(helix::ui::UpdateQueue::instance());

        REQUIRE(lv_subject_get_int(subject) == 0);
        REQUIRE(state.plugin_status_state().service_has_helix_plugin() == false);

        // Both return false for query, but subject values are different
        // This allows UI to distinguish "still checking" from "definitely not installed"
    }
}

// ============================================================================
// Async Update Tests - Verify thread-safe updates
// ============================================================================

TEST_CASE("Plugin status characterization: async update behavior",
          "[characterization][plugin][async]") {
    lv_init_safe();

    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(false);

    SECTION("set_helix_plugin_installed requires queue drain to take effect") {
        lv_subject_t* subject = state.plugin_status_state().get_helix_plugin_installed_subject();

        // Call setter but don't drain
        state.set_helix_plugin_installed(true);

        // Subject may still be -1 if queue hasn't processed
        // (This is implementation-dependent - the async call may be synchronous in tests)

        // Drain queue ensures update is processed
        UpdateQueueTestAccess::drain(helix::ui::UpdateQueue::instance());
        REQUIRE(lv_subject_get_int(subject) == 1);
    }

    SECTION("multiple rapid updates coalesce correctly") {
        lv_subject_t* subject = state.plugin_status_state().get_helix_plugin_installed_subject();

        // Rapid toggling
        state.set_helix_plugin_installed(true);
        state.set_helix_plugin_installed(false);
        state.set_helix_plugin_installed(true);
        UpdateQueueTestAccess::drain(helix::ui::UpdateQueue::instance());

        // Final value should be 1 (last write wins)
        REQUIRE(lv_subject_get_int(subject) == 1);
    }
}

// Note: tests for the legacy per-op `can_show_*` subjects were removed when
// those subjects were retired (no production consumer ever read them). The
// surviving aggregate `has_any_preprint_options` is exercised end-to-end by
// the print_file_detail tests.
