// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_drm_rotation_fallback.cpp
 * @brief Regression tests for DRM plane rotation fallback logic
 *
 * VC4 (Raspberry Pi with ultrawide panels) only supports 0/180 plane rotation.
 * Requesting 90/270 causes drmModeAtomicCommit to fail, breaking display init.
 * These tests verify that choose_drm_rotation_strategy() correctly decides
 * between hardware rotation, software fallback, or no rotation.
 *
 * DRM rotation bitmask bits (from drm_mode.h):
 *   DRM_MODE_ROTATE_0   = (1<<0) = 0x1
 *   DRM_MODE_ROTATE_90  = (1<<1) = 0x2
 *   DRM_MODE_ROTATE_180 = (1<<2) = 0x4
 *   DRM_MODE_ROTATE_270 = (1<<3) = 0x8
 */

#include "../catch_amalgamated.hpp"

// The function under test is pure logic, no DRM hardware needed.
// Include it even without HELIX_DISPLAY_DRM — the enum and function
// are deliberately kept hardware-independent for testability.
#include "drm_rotation_strategy.h"
#include "touch_calibration.h"

// DRM rotation constants (mirrored from drm_mode.h so tests compile
// without libdrm headers)
static constexpr uint64_t kRot0 = (1 << 0);   // 0x1
static constexpr uint64_t kRot90 = (1 << 1);  // 0x2
static constexpr uint64_t kRot180 = (1 << 2); // 0x4
static constexpr uint64_t kRot270 = (1 << 3); // 0x8

static constexpr uint64_t kMaskAll = kRot0 | kRot90 | kRot180 | kRot270; // 0xF
static constexpr uint64_t kMask0180 = kRot0 | kRot180;                   // 0x5 (VC4)
static constexpr uint64_t kMask0Only = kRot0;                            // 0x1
static constexpr uint64_t kMaskNone = 0;                                 // no rotation property

TEST_CASE("0° rotation always returns NONE", "[display][drm][rotation]") {
    // No rotation needed — no hardware or software path required
    REQUIRE(choose_drm_rotation_strategy(kRot0, kMaskAll) == DrmRotationStrategy::NONE);
    REQUIRE(choose_drm_rotation_strategy(kRot0, kMask0180) == DrmRotationStrategy::NONE);
    REQUIRE(choose_drm_rotation_strategy(kRot0, kMaskNone) == DrmRotationStrategy::NONE);
}

TEST_CASE("Hardware rotation when plane supports requested angle", "[display][drm][rotation]") {
    // Full rotation support (mask=0xF), request 180° → use hardware
    REQUIRE(choose_drm_rotation_strategy(kRot180, kMaskAll) == DrmRotationStrategy::HARDWARE);
}

TEST_CASE("90° and 270° never go to the plane, whatever its mask advertises",
          "[display][drm][rotation]") {
    // The plane's SRC and CRTC rectangles stay at the panel's own width and
    // height, and LVGL keeps laying out at that resolution, so a quarter turn
    // has nowhere coherent to land. amdgpu reports 0xF; Pi 3B vc4 reports 0x35.
    static constexpr uint64_t kReflectX = (1 << 4);
    static constexpr uint64_t kReflectY = (1 << 5);
    static constexpr uint64_t kMaskPi3b = kRot0 | kRot180 | kReflectX | kReflectY; // 0x35

    REQUIRE(choose_drm_rotation_strategy(kRot90, kMaskAll) == DrmRotationStrategy::SOFTWARE);
    REQUIRE(choose_drm_rotation_strategy(kRot270, kMaskAll) == DrmRotationStrategy::SOFTWARE);
    REQUIRE(choose_drm_rotation_strategy(kRot90, kRot90) == DrmRotationStrategy::SOFTWARE);
    REQUIRE(choose_drm_rotation_strategy(kRot270, kRot270) == DrmRotationStrategy::SOFTWARE);

    // The half turn on the same masks is the control: it still reaches the plane.
    REQUIRE(choose_drm_rotation_strategy(kRot180, kMaskPi3b) == DrmRotationStrategy::HARDWARE);
    REQUIRE(choose_drm_rotation_strategy(kRot90, kMaskPi3b) == DrmRotationStrategy::SOFTWARE);
}

TEST_CASE("EGL presentation rotates every nonzero angle on the GPU",
          "[display][drm][rotation][egl]") {
    // The presentation shader consumes LVGL's display rotation, so the plane's
    // rotation mask does not matter and scanout stays at the panel's native mode.
    REQUIRE(choose_drm_rotation_strategy(kRot90, kMaskNone, true) == DrmRotationStrategy::GPU);
    REQUIRE(choose_drm_rotation_strategy(kRot180, kMask0Only, true) == DrmRotationStrategy::GPU);
    REQUIRE(choose_drm_rotation_strategy(kRot270, kMask0180, true) == DrmRotationStrategy::GPU);

    // Identity still needs no presentation transform.
    REQUIRE(choose_drm_rotation_strategy(kRot0, kMaskNone, true) == DrmRotationStrategy::NONE);
}

TEST_CASE("Software fallback when plane lacks 90/270", "[display][drm][rotation]") {
    // VC4 scenario: mask=0x5 (0°+180°), request 270° → must use software
    REQUIRE(choose_drm_rotation_strategy(kRot270, kMask0180) == DrmRotationStrategy::SOFTWARE);
    REQUIRE(choose_drm_rotation_strategy(kRot90, kMask0180) == DrmRotationStrategy::SOFTWARE);
}

TEST_CASE("Software fallback when no rotation property", "[display][drm][rotation]") {
    // No rotation property at all (mask=0), any non-zero rotation → software
    REQUIRE(choose_drm_rotation_strategy(kRot90, kMaskNone) == DrmRotationStrategy::SOFTWARE);
    REQUIRE(choose_drm_rotation_strategy(kRot180, kMaskNone) == DrmRotationStrategy::SOFTWARE);
    REQUIRE(choose_drm_rotation_strategy(kRot270, kMaskNone) == DrmRotationStrategy::SOFTWARE);
}

TEST_CASE("180° uses hardware when supported", "[display][drm][rotation]") {
    // VC4 supports 180° — should use hardware path
    REQUIRE(choose_drm_rotation_strategy(kRot180, kMask0180) == DrmRotationStrategy::HARDWARE);
}

TEST_CASE("180° falls back to software when only 0° supported", "[display][drm][rotation]") {
    // Only 0° supported — 180° must use software
    REQUIRE(choose_drm_rotation_strategy(kRot180, kMask0Only) == DrmRotationStrategy::SOFTWARE);
}

TEST_CASE("HARDWARE rotation clears LVGL rotation", "[display][drm][rotation]") {
    // The plane rotates the scanout. LVGL rotating as well applies the
    // transform twice, and two 180s cancel (prestonbrown/helixscreen#1275).
    REQUIRE(lvgl_rotation_action_for(DrmRotationStrategy::HARDWARE) ==
            LvglRotationAction::CLEAR_TO_ZERO);
}

TEST_CASE("SOFTWARE rotation applies the requested angle to LVGL", "[display][drm][rotation]") {
    // The dumb-buffer flush callback reads lv_display_get_rotation() to decide
    // whether to reverse the pixel array, so LVGL must carry the angle.
    REQUIRE(lvgl_rotation_action_for(DrmRotationStrategy::SOFTWARE) ==
            LvglRotationAction::APPLY_REQUESTED);
}

TEST_CASE("GPU rotation exposes the requested angle to the presentation shader",
          "[display][drm][rotation][egl]") {
    REQUIRE(lvgl_rotation_action_for(DrmRotationStrategy::GPU) ==
            LvglRotationAction::APPLY_REQUESTED);
}

TEST_CASE("NONE clears LVGL rotation", "[display][drm][rotation]") {
    REQUIRE(lvgl_rotation_action_for(DrmRotationStrategy::NONE) ==
            LvglRotationAction::CLEAR_TO_ZERO);
}

TEST_CASE("Only SOFTWARE needs FULL render mode", "[display][drm][rotation]") {
    // A partial-render buffer cannot be reversed in place.
    REQUIRE(drm_rotation_needs_full_render(DrmRotationStrategy::SOFTWARE));
    REQUIRE_FALSE(drm_rotation_needs_full_render(DrmRotationStrategy::GPU));
    REQUIRE_FALSE(drm_rotation_needs_full_render(DrmRotationStrategy::HARDWARE));
    REQUIRE_FALSE(drm_rotation_needs_full_render(DrmRotationStrategy::NONE));
}

TEST_CASE("Plane may not own rotation until touch follows it", "[display][drm][rotation]") {
    // The plane may carry an angle now that DisplayBackendDRM transforms pointer
    // samples itself instead of relying on LVGL's display rotation, which the
    // HARDWARE path clears.
    REQUIRE(plane_may_own_rotation());
}

TEST_CASE("The plane transform composes over an affine placed in panel space",
          "[display][drm][rotation][touch-calibration]") {
    // DisplayBackendDRM chains the plane transform P over the calibration
    // wrapper, which places a logical-space affine A on the panel-space sample
    // it is handed. Composed that way a sample lands at A(P(raw)), the frame the
    // wizard solved A in. Composed the other way A runs on a sample P already
    // turned, and a translation lands doubled and mirrored.
    static constexpr int32_t kPanelW = 800;
    static constexpr int32_t kPanelH = 480;
    static constexpr int kPlaneDegrees = 180;

    helix::TouchCalibration cal;
    cal.valid = true;
    cal.c = 12.0f; // a = e = 1 and b = d = 0: a pure translation
    cal.f = -7.0f;
    // The wizard stamps display_rotation_degrees(), which is the plane's angle.
    cal.capture_rotation = kPlaneDegrees;

    const helix::Point raw{100, 50};

    const auto plane = [](helix::Point p) {
        const PointerXY turned =
            rotate_pointer_for_plane({p.x, p.y}, kPlaneDegrees, kPanelW, kPanelH);
        return helix::Point{turned.x, turned.y};
    };
    const auto calibrate = [&cal](helix::Point p) {
        return helix::apply_calibration_in_panel_space(cal, p, cal.capture_rotation, kPanelW,
                                                       kPanelH);
    };

    const helix::Point expected = helix::transform_point(cal, plane(raw), kPanelW - 1, kPanelH - 1);
    // Inside the panel on both axes, so no clamp can hide the translation.
    REQUIRE(expected.x == 799 - 100 + 12);
    REQUIRE(expected.y == 479 - 50 - 7);

    const helix::Point hook_over_calibration = plane(calibrate(raw));
    CHECK(hook_over_calibration.x == expected.x);
    CHECK(hook_over_calibration.y == expected.y);

    const helix::Point calibration_over_hook = calibrate(plane(raw));
    CHECK(calibration_over_hook.x == expected.x - 2 * 12);
    CHECK(calibration_over_hook.y == expected.y + 2 * 7);
}
