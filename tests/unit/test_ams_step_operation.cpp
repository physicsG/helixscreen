// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_ams_step_operation.cpp
 * @brief Unit tests for AMS step progress operation type detection
 *
 * Tests the pure detection logic in detect_step_operation() which determines
 * whether to show LOAD_FRESH, LOAD_SWAP, or UNLOAD step progress based on
 * action transitions and backend state.
 *
 * Key scenarios:
 * - External swap starting with HEATING (nozzle cold)
 * - External swap starting with CUTTING (nozzle already hot)
 * - External swap starting with UNLOADING (no cutter, nozzle hot)
 * - Fresh load (no filament loaded)
 * - Explicit unload
 * - Mid-operation upgrade from UNLOAD to LOAD_SWAP
 * - UI-initiated operations (not external) should not trigger detection
 */

#include "ams_step_operation.h"

#include "../catch_amalgamated.hpp"

// ============================================================================
// External Swap Detection (filament loaded, various start actions)
// ============================================================================

TEST_CASE("Step operation: external swap starting with HEATING", "[ams-step][step-detect]") {
    // Classic case: nozzle is cold, backend starts with HEATING
    auto result = detect_step_operation(AmsAction::HEATING, AmsAction::IDLE,
                                        StepOperationType::LOAD_FRESH, true, true);
    REQUIRE(result.should_recreate);
    REQUIRE(result.op_type == StepOperationType::LOAD_SWAP);
    REQUIRE(result.jump_to_step == -1);
}

TEST_CASE("Step operation: external swap starting with CUTTING", "[ams-step][step-detect]") {
    // Nozzle already hot, backend skips heating and goes straight to cutting
    auto result = detect_step_operation(AmsAction::CUTTING, AmsAction::IDLE,
                                        StepOperationType::LOAD_FRESH, true, true);
    REQUIRE(result.should_recreate);
    REQUIRE(result.op_type == StepOperationType::LOAD_SWAP);
}

TEST_CASE("Step operation: external swap starting with FORMING_TIP", "[ams-step][step-detect]") {
    // Nozzle hot, no cutter — tip-forming is the first action
    auto result = detect_step_operation(AmsAction::FORMING_TIP, AmsAction::IDLE,
                                        StepOperationType::LOAD_FRESH, true, true);
    REQUIRE(result.should_recreate);
    REQUIRE(result.op_type == StepOperationType::LOAD_SWAP);
}

TEST_CASE("Step operation: external swap starting with UNLOADING", "[ams-step][step-detect]") {
    // Nozzle hot, no cutter, no tip-forming — goes straight to unloading
    auto result = detect_step_operation(AmsAction::UNLOADING, AmsAction::IDLE,
                                        StepOperationType::LOAD_FRESH, true, true);
    REQUIRE(result.should_recreate);
    REQUIRE(result.op_type == StepOperationType::LOAD_SWAP);
}

// ============================================================================
// External Fresh Load (no filament loaded)
// ============================================================================

TEST_CASE("Step operation: external fresh load starting with HEATING", "[ams-step][step-detect]") {
    auto result = detect_step_operation(AmsAction::HEATING, AmsAction::IDLE,
                                        StepOperationType::LOAD_FRESH, true, false);
    REQUIRE(result.should_recreate);
    REQUIRE(result.op_type == StepOperationType::LOAD_FRESH);
}

TEST_CASE("Step operation: external fresh load starting with LOADING", "[ams-step][step-detect]") {
    // Nozzle already hot, goes straight to loading
    auto result = detect_step_operation(AmsAction::LOADING, AmsAction::IDLE,
                                        StepOperationType::LOAD_FRESH, true, false);
    REQUIRE(result.should_recreate);
    REQUIRE(result.op_type == StepOperationType::LOAD_FRESH);
}

TEST_CASE("Step operation: LOADING action always means LOAD_FRESH even if filament loaded",
          "[ams-step][step-detect]") {
    // If the first action is LOADING, it's always a fresh load — the backend has
    // already handled any unloading before reporting LOADING.
    auto result = detect_step_operation(AmsAction::LOADING, AmsAction::IDLE,
                                        StepOperationType::LOAD_FRESH, true, true);
    REQUIRE(result.should_recreate);
    REQUIRE(result.op_type == StepOperationType::LOAD_FRESH);
}

// ============================================================================
// External Unload
// ============================================================================

TEST_CASE("Step operation: explicit unload (UNLOADING after non-cutting prev)",
          "[ams-step][step-detect]") {
    // UNLOADING arrives, prev was HEATING (not CUTTING/FORMING_TIP), not in LOAD_SWAP
    auto result = detect_step_operation(AmsAction::UNLOADING, AmsAction::HEATING,
                                        StepOperationType::LOAD_FRESH, true, false);
    REQUIRE(result.should_recreate);
    REQUIRE(result.op_type == StepOperationType::UNLOAD);
}

TEST_CASE("Step operation: UNLOADING after CUTTING does not recreate as UNLOAD",
          "[ams-step][step-detect]") {
    // UNLOADING follows CUTTING — this is part of a swap, don't override
    auto result = detect_step_operation(AmsAction::UNLOADING, AmsAction::CUTTING,
                                        StepOperationType::LOAD_SWAP, true, false);
    REQUIRE_FALSE(result.should_recreate);
}

TEST_CASE("Step operation: UNLOADING after FORMING_TIP does not recreate as UNLOAD",
          "[ams-step][step-detect]") {
    // UNLOADING follows FORMING_TIP — this is part of a swap, don't override
    auto result = detect_step_operation(AmsAction::UNLOADING, AmsAction::FORMING_TIP,
                                        StepOperationType::LOAD_SWAP, true, false);
    REQUIRE_FALSE(result.should_recreate);
}

TEST_CASE("Step operation: UNLOADING does not override LOAD_SWAP", "[ams-step][step-detect]") {
    // Already in LOAD_SWAP mode, UNLOADING comes from a non-cutting prev
    // (e.g., after HEATING) — should not downgrade to UNLOAD
    auto result = detect_step_operation(AmsAction::UNLOADING, AmsAction::HEATING,
                                        StepOperationType::LOAD_SWAP, true, false);
    REQUIRE_FALSE(result.should_recreate);
}

// ============================================================================
// Mid-Operation Upgrade: UNLOAD → LOAD_SWAP
// ============================================================================

TEST_CASE("Step operation: upgrade UNLOAD to LOAD_SWAP when LOADING arrives",
          "[ams-step][step-detect]") {
    // Was showing UNLOAD, but loading started — this is actually a swap
    auto result = detect_step_operation(AmsAction::LOADING, AmsAction::UNLOADING,
                                        StepOperationType::UNLOAD, true, false);
    REQUIRE(result.should_recreate);
    REQUIRE(result.op_type == StepOperationType::LOAD_SWAP);
    REQUIRE(result.jump_to_step == 2); // Skip heat + cut/tip steps
}

TEST_CASE("Step operation: LOADING during LOAD_SWAP does not recreate", "[ams-step][step-detect]") {
    // Already in LOAD_SWAP, LOADING is expected — no recreate needed
    auto result = detect_step_operation(AmsAction::LOADING, AmsAction::UNLOADING,
                                        StepOperationType::LOAD_SWAP, true, false);
    REQUIRE_FALSE(result.should_recreate);
}

// ============================================================================
// UI-Initiated Operations (not external)
// ============================================================================

TEST_CASE("Step operation: UI-initiated operations are never detected", "[ams-step][step-detect]") {
    // is_external = false — operation was started by our UI via start_operation()
    // Detection should NOT trigger; the UI already set the correct operation type.
    auto result = detect_step_operation(AmsAction::HEATING, AmsAction::IDLE,
                                        StepOperationType::LOAD_FRESH, false, true);
    REQUIRE_FALSE(result.should_recreate);
}

TEST_CASE("Step operation: UI-initiated UNLOAD not overridden", "[ams-step][step-detect]") {
    auto result = detect_step_operation(AmsAction::UNLOADING, AmsAction::HEATING,
                                        StepOperationType::UNLOAD, false, false);
    REQUIRE_FALSE(result.should_recreate);
}

TEST_CASE("Step operation: UI-initiated LOAD_SWAP not upgraded", "[ams-step][step-detect]") {
    auto result = detect_step_operation(AmsAction::LOADING, AmsAction::UNLOADING,
                                        StepOperationType::UNLOAD, false, false);
    REQUIRE_FALSE(result.should_recreate);
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST_CASE("Step operation: IDLE action never triggers detection", "[ams-step][step-detect]") {
    auto result = detect_step_operation(AmsAction::IDLE, AmsAction::IDLE,
                                        StepOperationType::LOAD_FRESH, true, true);
    REQUIRE_FALSE(result.should_recreate);
}

TEST_CASE("Step operation: ERROR action never triggers detection", "[ams-step][step-detect]") {
    auto result = detect_step_operation(AmsAction::ERROR, AmsAction::LOADING,
                                        StepOperationType::LOAD_FRESH, true, true);
    REQUIRE_FALSE(result.should_recreate);
}

TEST_CASE("Step operation: non-IDLE to active does not trigger initial detection",
          "[ams-step][step-detect]") {
    // prev != IDLE — this is a mid-operation transition, not a new operation start
    // (unless it matches the unload or upgrade paths)
    auto result = detect_step_operation(AmsAction::HEATING, AmsAction::LOADING,
                                        StepOperationType::LOAD_FRESH, true, true);
    REQUIRE_FALSE(result.should_recreate);
}

// =============================================================================
// A UI-initiated unload keeps its unload bar
//
// Pressing Unload built the 4-step unload bar and then, ~450ms later, replaced
// it with the 5-step LOAD bar parked on "Feed filament" — which is what the
// user saw for the rest of the operation. Traced on hardware to the
// external-start arm: UNLOADING with filament loaded reads as "the unload half
// of a swap".
//
// The arm's guards do not exclude it. `is_external` means target_load_slot_ < 0,
// which the sidebar clears on any non-progress action, so one transient IDLE
// mid-unload makes the UI's own operation look foreign — and that same
// transient is what leaves prev_action at IDLE.
// =============================================================================

TEST_CASE("An in-progress explicit unload is not reinterpreted as a swap",
          "[ams][step_operation][1229]") {
    SECTION("the exact hardware shape: unload misread as a swap") {
        // is_external and prev_action=IDLE both come from the transient, and
        // filament_loaded is true because the filament has not left yet.
        const auto r =
            detect_step_operation(AmsAction::UNLOADING, AmsAction::IDLE, StepOperationType::UNLOAD,
                                  /*is_external=*/true, /*filament_loaded=*/true);
        CHECK_FALSE(r.should_recreate); // keep the 4-step unload bar
    }

    SECTION("still true however it is observed") {
        for (bool external : {false, true}) {
            for (bool loaded : {false, true}) {
                for (auto prev : {AmsAction::IDLE, AmsAction::HEATING, AmsAction::UNLOADING}) {
                    INFO("external=" << external << " loaded=" << loaded);
                    const auto r = detect_step_operation(
                        AmsAction::UNLOADING, prev, StepOperationType::UNLOAD, external, loaded);
                    CHECK_FALSE(r.should_recreate);
                }
            }
        }
    }

    SECTION("a genuine swap still upgrades when loading starts") {
        // The designed route, and the reason the guard is scoped to UNLOADING:
        // once the machine starts FEEDING, this really is a swap.
        const auto r = detect_step_operation(AmsAction::LOADING, AmsAction::UNLOADING,
                                             StepOperationType::UNLOAD,
                                             /*is_external=*/true, /*filament_loaded=*/false);
        CHECK(r.should_recreate);
        CHECK(r.op_type == StepOperationType::LOAD_SWAP);
    }

    SECTION("an externally-started unload is still detected") {
        // Nothing was declared by the UI, so the guess is all there is — a swap
        // guess here is still correct behaviour and must not regress.
        const auto r = detect_step_operation(AmsAction::UNLOADING, AmsAction::IDLE,
                                             StepOperationType::LOAD_FRESH,
                                             /*is_external=*/true, /*filament_loaded=*/true);
        CHECK(r.should_recreate);
        CHECK(r.op_type == StepOperationType::LOAD_SWAP);
    }
}

// =============================================================================
// Operation ownership survives the pre-start lag
//
// start_operation() optimistically sets HEATING and the backend's still-IDLE
// truth lands on top before the firmware picks the op up. The old signal
// (target_load_slot_ < 0, cleared on any non-running action) could not tell
// that from completion, so a UI-initiated unload was declared foreign
// mid-flight and re-read as the unload half of a swap.
// =============================================================================

TEST_CASE("Ownership tells pre-start lag apart from completion",
          "[ams][step_operation][ownership]") {
    SECTION("an idle BEFORE the op is seen running keeps ownership") {
        OperationOwnership own;
        own.on_start();
        own.on_action(false); // backend has not caught up yet
        CHECK_FALSE(own.is_external());
        own.on_action(false); // still not; ownership must not erode
        CHECK_FALSE(own.is_external());
    }

    SECTION("an idle AFTER it ran releases ownership") {
        OperationOwnership own;
        own.on_start();
        own.on_action(true); // firmware picked it up
        CHECK_FALSE(own.is_external());
        own.on_action(false); // finished
        CHECK(own.is_external());
    }

    SECTION("the full hardware sequence") {
        // start -> transient idle -> running -> ... -> idle
        OperationOwnership own;
        own.on_start();
        own.on_action(false); // 11:00:02.393-.844, the window that broke it
        CHECK_FALSE(own.is_external());
        for (int i = 0; i < 5; ++i) {
            own.on_action(true);
            CHECK_FALSE(own.is_external());
        }
        own.on_action(false);
        CHECK(own.is_external());
    }

    SECTION("nothing started is external, and stays so") {
        OperationOwnership own;
        CHECK(own.is_external());
        own.on_action(true); // someone else's operation
        CHECK(own.is_external());
        own.on_action(false);
        CHECK(own.is_external());
    }

    SECTION("a refused dispatch releases immediately") {
        // No running action will ever arrive to end it, so on_action() alone
        // would leave ownership stuck on forever.
        OperationOwnership own;
        own.on_start();
        own.on_abandon();
        CHECK(own.is_external());
    }

    SECTION("a second start re-arms the latch") {
        OperationOwnership own;
        own.on_start();
        own.on_action(true);
        own.on_start(); // new op before the old one's idle arrived
        own.on_action(false);
        CHECK_FALSE(own.is_external()); // pre-start lag again, not completion
    }
}

// =============================================================================
// A swap bar survives the load half
//
// The mirror of the UNLOAD guard above, and the defect it fixes is the same
// one: a UI-initiated swap that preheats first spends the wait at HEATING and
// then IDLE, which the ownership latch reads as "finished". The operation then
// looks foreign, and the external-start arm rebuilt the multiACE 7-step swap
// bar as the 5-step fresh-load one at the moment the backend reported LOADING,
// discarding the retract half mid-operation.
// =============================================================================

TEST_CASE("a LOAD_SWAP bar is not demoted when loading starts", "[ams][step_operation][swap]") {
    // Exactly the transient: prev IDLE (the preheat gap), LOADING now, and the
    // operation misread as external.
    auto result = detect_step_operation(AmsAction::LOADING, AmsAction::IDLE,
                                        StepOperationType::LOAD_SWAP, /*is_external=*/true,
                                        /*filament_loaded=*/true);
    CHECK_FALSE(result.should_recreate);
}

TEST_CASE("an unloaded head still starts a fresh-load bar", "[ams][step_operation][swap]") {
    // The guard must not swallow a genuine fresh load: with a LOAD_FRESH bar up
    // the external-start arm still fires and still answers LOAD_FRESH.
    auto result = detect_step_operation(AmsAction::LOADING, AmsAction::IDLE,
                                        StepOperationType::LOAD_FRESH, /*is_external=*/true,
                                        /*filament_loaded=*/false);
    CHECK(result.should_recreate);
    CHECK(result.op_type == StepOperationType::LOAD_FRESH);
}

TEST_CASE("an unload can still be upgraded to a swap", "[ams][step_operation][swap]") {
    // The designed route for a swap the printer started itself must survive:
    // LOADING while an UNLOAD bar is up is still the mid-operation upgrade.
    auto result = detect_step_operation(AmsAction::LOADING, AmsAction::UNLOADING,
                                        StepOperationType::UNLOAD, /*is_external=*/true,
                                        /*filament_loaded=*/true);
    CHECK(result.should_recreate);
    CHECK(result.op_type == StepOperationType::LOAD_SWAP);
}
