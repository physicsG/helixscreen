// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_print_start_filament_gate.cpp
 * @brief Tests for PrintStartController::check_required_filament_present() — the
 *        shared pre-print filament gate (review issue 1).
 *
 * Run with: ./build/bin/helix-tests "[print-start][filament-gate]"
 *
 * Regression context: the insufficient-filament "Proceed" continuation used to
 * re-run the lane-truth check WITHOUT the auto_unloads_after_print() suppression
 * that initiate() applied. On AD5X IFS (which retracts filament from the toolhead
 * at end-of-print by design, so lanes read empty at the next print-start) that
 * produced a deterministic FALSE "no filament" warning. The fix factors the
 * suppression + filament-present check into ONE shared method so both paths
 * behave identically. These tests exercise that method directly.
 */

#include "ui_print_start_controller.h"

#include "../helix_test_fixture.h"
#include "ams_backend_ad5x_ifs.h"
#include "ams_state.h"
#include "filament_sensor_manager.h"
#include "moonraker_api_mock.h"
#include "moonraker_client_mock.h"
#include "printer_state.h"

#include <memory>

#include "../catch_amalgamated.hpp"

using namespace helix;
using namespace helix::ui;

// Friend shim lives in test_helpers/ — it must be defined exactly once across
// the test binary (see the header's note on ODR).
#include "../test_helpers/print_start_controller_test_access.h"

namespace {

// RAII: install a real AD5X IFS backend (auto_unloads_after_print() == true)
// into AmsState and remove it on scope exit.
struct ScopedAd5xIfsBackend {
    MoonrakerClientMock client{MoonrakerClientMock::PrinterType::VORON_24};
    PrinterState state;
    std::unique_ptr<MoonrakerAPIMock> api;
    AmsBackendAd5xIfs* backend = nullptr;

    ScopedAd5xIfsBackend() {
        state.init_subjects(false);
        api = std::make_unique<MoonrakerAPIMock>(client, state);
        auto be = std::make_unique<AmsBackendAd5xIfs>(api.get(), nullptr);
        backend = be.get();
        AmsState::instance().set_backend(std::move(be));
    }
    ~ScopedAd5xIfsBackend() {
        AmsState::instance().set_backend(nullptr);
    }
};

} // namespace

TEST_CASE("print-start gate (issue 1): AD5X IFS auto-unload suppresses the warning",
          "[print-start][filament-gate]") {
    HelixTestFixture fx;
    ScopedAd5xIfsBackend ifs;

    // A fresh IFS backend reports its lanes empty (no filament parsed yet) — the
    // exact post-auto-unload state that used to false-positive on the
    // insufficient-filament continuation path. is_present() would be false for
    // every lane.
    REQUIRE(ifs.backend->auto_unloads_after_print());

    MoonrakerClientMock client{MoonrakerClientMock::PrinterType::VORON_24};
    PrinterState ps;
    ps.init_subjects(false);
    MoonrakerAPIMock api(client, ps);
    PrintStartController controller(ps, &api);
    // No detail view set: the suppression branch must return BEFORE touching it.

    // The shared gate must SUPPRESS (return false = no warning) because the
    // active backend auto-unloads after print. This is the same method the
    // insufficient-filament Proceed path now calls, so that path is suppressed
    // identically (issue 1 regression closed).
    CHECK_FALSE(PrintStartControllerTestAccess::check(controller));
}

TEST_CASE("print-start gate (issue 1): no backend + no runout sensor -> no warning",
          "[print-start][filament-gate]") {
    HelixTestFixture fx;
    AmsState::instance().set_backend(nullptr);
    // Clear any RUNOUT sensor left by a prior test in this process (the manager
    // is a singleton); discover_sensors({}) clears the sensor list + roles.
    FilamentSensorManager::instance().discover_sensors({});

    MoonrakerClientMock client{MoonrakerClientMock::PrinterType::VORON_24};
    PrinterState ps;
    ps.init_subjects(false);
    MoonrakerAPIMock api(client, ps);
    PrintStartController controller(ps, &api);

    // No AMS backend, no configured runout sensor -> the aggregate fallback finds
    // nothing to warn about. Gate returns false (proceed).
    CHECK_FALSE(PrintStartControllerTestAccess::check(controller));
}

// ============================================================================
// Remap-unsupported warning discriminator (Snapmaker U1 stale-toast bug).
//
// apply_filament_remaps() must NOT toast "remap not supported" for a backend
// that applies the remap via its firmware-native pre-print path
// (requires_preprint_send) — build_preprint_gcode() honors the user's choice
// even though get_tool_mapping_capabilities() reports editable=false. The toast
// is only correct for a backend that can NEITHER edit its mapping NOR apply it
// via a pre-print send.
// ============================================================================

namespace {
helix::printer::ToolMappingCapabilities caps(bool supported, bool editable) {
    return {supported, editable, ""};
}
} // namespace

TEST_CASE("remap warning: Snapmaker (editable=false + pre-print) is SILENT",
          "[print-start][filament-gate][remap]") {
    // Real AmsBackendSnapmaker: get_tool_mapping_capabilities() -> {false,false}
    // (default), requires_preprint_send() -> true. The remap flows through
    // build_preprint_gcode, so no warning.
    auto& A = PrintStartControllerTestAccess::should_warn_remap_unsupported;
    CHECK_FALSE(A(caps(/*supported=*/false, /*editable=*/false), /*applies_via_preprint=*/true));
    // Mock snapmaker_mode reports {true,false} + pre-print — also silent.
    CHECK_FALSE(A(caps(/*supported=*/true, /*editable=*/false), /*applies_via_preprint=*/true));
}

TEST_CASE("remap warning: genuinely-incapable backend (no edit, no pre-print) WARNS",
          "[print-start][filament-gate][remap]") {
    // ACE-like: {false,false} capabilities AND no pre-print send -> the remap
    // genuinely cannot be honored, so the warning is correct.
    auto& A = PrintStartControllerTestAccess::should_warn_remap_unsupported;
    CHECK(A(caps(/*supported=*/false, /*editable=*/false), /*applies_via_preprint=*/false));
    // supported-but-not-editable AND no pre-print -> also unhonorable -> warn.
    CHECK(A(caps(/*supported=*/true, /*editable=*/false), /*applies_via_preprint=*/false));
}

TEST_CASE("remap warning: editable backend never warns (generic remap path is live)",
          "[print-start][filament-gate][remap]") {
    // AFC / ToolChanger / QIDI / CFS / AD5X-IFS report editable=true: the generic
    // set_tool_mapping() path in apply_filament_remaps honors the remap, so the
    // decision helper is never even reached for the warning — but assert the
    // predicate is false for editable backends regardless of pre-print flag.
    auto& A = PrintStartControllerTestAccess::should_warn_remap_unsupported;
    CHECK_FALSE(A(caps(/*supported=*/true, /*editable=*/true), /*applies_via_preprint=*/false));
    CHECK_FALSE(A(caps(/*supported=*/true, /*editable=*/true), /*applies_via_preprint=*/true));
}
