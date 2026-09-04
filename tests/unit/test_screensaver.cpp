// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../lvgl_test_fixture.h"
#include "config.h"
#include "display_settings_manager.h"
#include "platform_capabilities.h"

#include "../catch_amalgamated.hpp"

using namespace helix;

// ============================================================================
// Screensaver Settings Tests
// ============================================================================

#ifdef HELIX_ENABLE_SCREENSAVER

TEST_CASE_METHOD(LVGLTestFixture,
                 "Screensaver defaults to tier-appropriate screensaver type when compiled in",
                 "[screensaver][display_settings]") {
    Config::get_instance();
    DisplaySettingsManager::instance().init_subjects();

    int expected = helix::PlatformCapabilities::detect().supports_animations ? 1 : 0;
    REQUIRE(DisplaySettingsManager::instance().get_screensaver_type() == expected);

    DisplaySettingsManager::instance().deinit_subjects();
}

TEST_CASE_METHOD(LVGLTestFixture, "Screensaver type set/get round trip",
                 "[screensaver][display_settings]") {
    Config::get_instance();
    DisplaySettingsManager::instance().init_subjects();

    SECTION("set to Off") {
        DisplaySettingsManager::instance().set_screensaver_type(0);
        REQUIRE(DisplaySettingsManager::instance().get_screensaver_type() == 0);
    }

    SECTION("set to Starfield") {
        DisplaySettingsManager::instance().set_screensaver_type(2);
        REQUIRE(DisplaySettingsManager::instance().get_screensaver_type() == 2);
    }

    SECTION("set to 3D Pipes") {
        DisplaySettingsManager::instance().set_screensaver_type(3);
        REQUIRE(DisplaySettingsManager::instance().get_screensaver_type() == 3);
    }

    SECTION("set back to Flying Toasters") {
        DisplaySettingsManager::instance().set_screensaver_type(0);
        DisplaySettingsManager::instance().set_screensaver_type(1);
        REQUIRE(DisplaySettingsManager::instance().get_screensaver_type() == 1);
    }

    SECTION("out of range clamped") {
        DisplaySettingsManager::instance().set_screensaver_type(99);
        REQUIRE(DisplaySettingsManager::instance().get_screensaver_type() == 3);

        DisplaySettingsManager::instance().set_screensaver_type(-1);
        REQUIRE(DisplaySettingsManager::instance().get_screensaver_type() == 0);
    }

    DisplaySettingsManager::instance().deinit_subjects();
}

TEST_CASE_METHOD(LVGLTestFixture, "Screensaver type subject reflects setter",
                 "[screensaver][display_settings]") {
    Config::get_instance();
    DisplaySettingsManager::instance().init_subjects();

    DisplaySettingsManager::instance().set_screensaver_type(0);
    REQUIRE(lv_subject_get_int(DisplaySettingsManager::instance().subject_screensaver_type()) == 0);

    DisplaySettingsManager::instance().set_screensaver_type(2);
    REQUIRE(lv_subject_get_int(DisplaySettingsManager::instance().subject_screensaver_type()) == 2);

    DisplaySettingsManager::instance().set_screensaver_type(1);
    REQUIRE(lv_subject_get_int(DisplaySettingsManager::instance().subject_screensaver_type()) == 1);

    DisplaySettingsManager::instance().deinit_subjects();
}

// ============================================================================
// FlyingToasterScreensaver Lifecycle Tests
// ============================================================================

#include "ui_screensaver.h"

TEST_CASE_METHOD(LVGLTestFixture, "FlyingToasterScreensaver starts inactive", "[screensaver]") {
    FlyingToasterScreensaver ss;
    REQUIRE(ss.is_active() == false);
}

TEST_CASE_METHOD(LVGLTestFixture, "FlyingToasterScreensaver start/stop lifecycle",
                 "[screensaver]") {
    FlyingToasterScreensaver ss;

    SECTION("start activates screensaver") {
        ss.start();
        REQUIRE(ss.is_active() == true);
        ss.stop();
    }

    SECTION("stop deactivates screensaver") {
        ss.start();
        ss.stop();
        REQUIRE(ss.is_active() == false);
    }

    SECTION("double start is safe") {
        ss.start();
        ss.start();
        REQUIRE(ss.is_active() == true);
        ss.stop();
    }

    SECTION("double stop is safe") {
        ss.start();
        ss.stop();
        ss.stop();
        REQUIRE(ss.is_active() == false);
    }

    SECTION("stop without start is safe") {
        ss.stop();
        REQUIRE(ss.is_active() == false);
    }
}

TEST_CASE_METHOD(LVGLTestFixture, "FlyingToasterScreensaver creates overlay on lv_layer_top",
                 "[screensaver]") {
    FlyingToasterScreensaver ss;

    int children_before = lv_obj_get_child_count(lv_layer_top());
    ss.start();
    int children_after = lv_obj_get_child_count(lv_layer_top());
    REQUIRE(children_after > children_before);

    ss.stop();
    REQUIRE_FALSE(ss.is_active());
    // Overlay is hidden and queued for async deletion (can't flush without
    // running all timers, which disrupts other fixture state)
    int children_final = lv_obj_get_child_count(lv_layer_top());
    REQUIRE(children_final <= children_after);
}

// ============================================================================
// ScreensaverManager Tests
// ============================================================================

#include "screensaver.h"

TEST_CASE_METHOD(LVGLTestFixture, "ScreensaverManager starts inactive", "[screensaver]") {
    REQUIRE(ScreensaverManager::instance().is_active() == false);
}

TEST_CASE_METHOD(LVGLTestFixture, "ScreensaverManager start/stop lifecycle", "[screensaver]") {
    auto& mgr = ScreensaverManager::instance();

    SECTION("start OFF does nothing") {
        mgr.start(ScreensaverType::OFF);
        REQUIRE(mgr.is_active() == false);
    }

    SECTION("start and stop Flying Toasters") {
        mgr.start(ScreensaverType::FLYING_TOASTERS);
        REQUIRE(mgr.is_active() == true);
        mgr.stop();
        REQUIRE(mgr.is_active() == false);
    }

    SECTION("start and stop Starfield") {
        mgr.start(ScreensaverType::STARFIELD);
        REQUIRE(mgr.is_active() == true);
        mgr.stop();
        REQUIRE(mgr.is_active() == false);
    }

    SECTION("start and stop 3D Pipes") {
        mgr.start(ScreensaverType::PIPES_3D);
        REQUIRE(mgr.is_active() == true);
        mgr.stop();
        REQUIRE(mgr.is_active() == false);
    }

    SECTION("switching types stops previous") {
        mgr.start(ScreensaverType::FLYING_TOASTERS);
        REQUIRE(mgr.is_active() == true);
        mgr.start(ScreensaverType::STARFIELD);
        REQUIRE(mgr.is_active() == true);
        mgr.stop();
        REQUIRE(mgr.is_active() == false);
    }

    SECTION("double stop is safe") {
        mgr.start(ScreensaverType::FLYING_TOASTERS);
        mgr.stop();
        mgr.stop();
        REQUIRE(mgr.is_active() == false);
    }
}

#endif // HELIX_ENABLE_SCREENSAVER
