// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_wizard_touch_calibration_force.cpp
 * @brief Regression test for wizard touch-calibration force-override
 *
 * Pins the contract that `force_touch_calibration_step(true)` makes
 * WizardTouchCalibrationStep::should_skip() return false even when other
 * conditions (non-framebuffer build, prior `/input/calibration/valid=true`)
 * would otherwise auto-skip the step.
 *
 * Why this matters: with a first-run wizard pending and a stale-but-"valid"
 * calibration on disk, the wizard's step-0 auto-skip would silently swallow
 * an explicit user request (HELIX_TOUCH_CALIBRATE=1 / --calibrate-touch).
 * The application-layer fix (src/application/application.cpp run_wizard())
 * calls force_touch_calibration_step(true) on that path; this test pins the
 * downstream invariant that the call actually flips should_skip().
 */

#include "ui_wizard_touch_calibration.h"

#include <spdlog/spdlog.h>

#include "../catch_amalgamated.hpp"

// ============================================================================
// Test Fixture
// ============================================================================

class WizardTouchCalForceFixture {
  public:
    WizardTouchCalForceFixture() {
        // Reset to a known state — the force flag is process-static and
        // tests may run in any order.
        force_touch_calibration_step(false);
    }

    ~WizardTouchCalForceFixture() {
        // Always leave the flag off so we don't leak state into peers.
        force_touch_calibration_step(false);
    }
};

// ============================================================================
// Tests
// ============================================================================

TEST_CASE_METHOD(WizardTouchCalForceFixture,
                 "WizardTouchCalibrationStep - force flag overrides auto-skip",
                 "[wizard][touch-calibration][skip]") {
    WizardTouchCalibrationStep step;

    SECTION("Default (no force): step is skipped on non-fbdev test build") {
        // Host/SDL builds do not define HELIX_DISPLAY_FBDEV, so the
        // non-framebuffer branch in should_skip() returns true. This is
        // the baseline behavior the wizard relies on.
        REQUIRE(step.should_skip() == true);
    }

    SECTION("Force flag set: should_skip() returns false") {
        // Regression: with the force flag set, should_skip() MUST return
        // false BEFORE any other check (non-fbdev / already-calibrated /
        // device doesn't need calibration). This is the invariant that
        // makes HELIX_TOUCH_CALIBRATE / --calibrate-touch effective while
        // the first-run wizard is pending.
        force_touch_calibration_step(true);
        REQUIRE(step.should_skip() == false);
    }

    SECTION("Force flag cleared: original skip behavior restored") {
        force_touch_calibration_step(true);
        REQUIRE(step.should_skip() == false);
        force_touch_calibration_step(false);
        REQUIRE(step.should_skip() == true);
    }
}
