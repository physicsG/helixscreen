// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_runtime_config.cpp
 * @brief Unit tests for RuntimeConfig mock selection logic
 *
 * Tests the should_mock_* methods that control which components use
 * mock implementations vs real hardware.
 */

#include "runtime_config.h"

#include "../catch_amalgamated.hpp"

// ============================================================================
// Production Mode Tests (test_mode = false)
// ============================================================================

TEST_CASE("RuntimeConfig - production mode never uses mocks", "[runtime_config]") {
    RuntimeConfig config;
    config.test_mode = false;

    SECTION("WiFi uses real backend in production") {
        config.use_real_wifi = false; // Even when not explicitly requested
        REQUIRE_FALSE(config.should_mock_wifi());
    }

    SECTION("Ethernet uses real backend in production") {
        config.use_real_ethernet = false;
        REQUIRE_FALSE(config.should_mock_ethernet());
    }

    SECTION("Moonraker uses real backend in production") {
        config.use_real_moonraker = false;
        REQUIRE_FALSE(config.should_mock_moonraker());
    }

    SECTION("Files use real backend in production") {
        config.use_real_files = false;
        REQUIRE_FALSE(config.should_use_test_files());
    }

    SECTION("AMS uses real backend in production") {
        config.use_real_ams = false;
        REQUIRE_FALSE(config.should_mock_ams());
    }

    SECTION("USB uses real backend in production") {
        REQUIRE_FALSE(config.should_mock_usb());
    }

    SECTION("mDNS uses real backend in production") {
        REQUIRE_FALSE(config.should_mock_mdns());
    }

    SECTION("Sensors use real backend in production") {
        config.use_real_sensors = false;
        REQUIRE_FALSE(config.should_mock_sensors());
    }
}

// ============================================================================
// Test Mode Default Behavior (mocks enabled by default)
// ============================================================================

TEST_CASE("RuntimeConfig - test mode uses mocks by default", "[runtime_config]") {
    RuntimeConfig config;
    config.test_mode = true;

    SECTION("WiFi uses mock by default in test mode") {
        REQUIRE(config.should_mock_wifi());
    }

    SECTION("Ethernet uses mock by default in test mode") {
        REQUIRE(config.should_mock_ethernet());
    }

    SECTION("Moonraker uses mock by default in test mode") {
        REQUIRE(config.should_mock_moonraker());
    }

    SECTION("Files use test data by default in test mode") {
        REQUIRE(config.should_use_test_files());
    }

    SECTION("AMS uses mock by default in test mode") {
        REQUIRE(config.should_mock_ams());
    }

    SECTION("USB uses mock in test mode") {
        REQUIRE(config.should_mock_usb());
    }

    SECTION("mDNS is skipped in test mode") {
        REQUIRE(config.should_mock_mdns());
    }

    SECTION("Sensors use mock by default in test mode") {
        REQUIRE(config.should_mock_sensors());
    }
}

// ============================================================================
// Test Mode with Real Overrides (--real-* flags)
// ============================================================================

TEST_CASE("RuntimeConfig - test mode respects --real-* overrides", "[runtime_config]") {
    RuntimeConfig config;
    config.test_mode = true;

    SECTION("--real-wifi disables WiFi mock") {
        config.use_real_wifi = true;
        REQUIRE_FALSE(config.should_mock_wifi());
    }

    SECTION("--real-ethernet disables Ethernet mock") {
        config.use_real_ethernet = true;
        REQUIRE_FALSE(config.should_mock_ethernet());
    }

    SECTION("--real-moonraker disables Moonraker mock") {
        config.use_real_moonraker = true;
        REQUIRE_FALSE(config.should_mock_moonraker());
    }

    SECTION("--real-files disables test file data") {
        config.use_real_files = true;
        REQUIRE_FALSE(config.should_use_test_files());
    }

    SECTION("--real-ams disables AMS mock") {
        config.use_real_ams = true;
        REQUIRE_FALSE(config.should_mock_ams());
    }

    SECTION("--real-sensors disables sensor mock") {
        config.use_real_sensors = true;
        REQUIRE_FALSE(config.should_mock_sensors());
    }
}

// ============================================================================
// AMS Special Case (--no-ams flag)
// ============================================================================

TEST_CASE("RuntimeConfig - AMS mock can be disabled without using real AMS", "[runtime_config]") {
    RuntimeConfig config;
    config.test_mode = true;

    SECTION("--no-ams disables mock AMS creation") {
        config.disable_mock_ams = true;
        REQUIRE_FALSE(config.should_mock_ams());
    }

    SECTION("--real-ams takes precedence over --no-ams") {
        config.use_real_ams = true;
        config.disable_mock_ams = true;
        REQUIRE_FALSE(config.should_mock_ams());
    }
}

// ============================================================================
// Splash Screen Logic
// ============================================================================

TEST_CASE("RuntimeConfig - splash screen skip logic", "[runtime_config]") {
    RuntimeConfig config;

    SECTION("Splash shown in production mode by default") {
        config.test_mode = false;
        config.skip_splash = false;
        REQUIRE_FALSE(config.should_skip_splash());
    }

    SECTION("--skip-splash flag skips splash") {
        config.test_mode = false;
        config.skip_splash = true;
        REQUIRE(config.should_skip_splash());
    }

    SECTION("Test mode always skips splash") {
        config.test_mode = true;
        config.skip_splash = false;
        REQUIRE(config.should_skip_splash());
    }
}

// ============================================================================
// Helper Methods
// ============================================================================

TEST_CASE("RuntimeConfig - is_test_mode helper", "[runtime_config]") {
    RuntimeConfig config;

    SECTION("Returns false when test_mode is false") {
        config.test_mode = false;
        REQUIRE_FALSE(config.is_test_mode());
    }

    SECTION("Returns true when test_mode is true") {
        config.test_mode = true;
        REQUIRE(config.is_test_mode());
    }
}

TEST_CASE("RuntimeConfig - get_default_test_file_path", "[runtime_config]") {
    const char* path = RuntimeConfig::get_default_test_file_path();

    REQUIRE(path != nullptr);
    REQUIRE(std::string(path) == "assets/test_gcodes/3DBenchy.gcode");
}

TEST_CASE("RuntimeConfig - static constants", "[runtime_config]") {
    REQUIRE(std::string(RuntimeConfig::TEST_GCODE_DIR) == "assets/test_gcodes");
    REQUIRE(std::string(RuntimeConfig::DEFAULT_TEST_FILE) == "3DBenchy.gcode");
    REQUIRE(std::string(RuntimeConfig::PROD_CONFIG_PATH) == "config/settings.json");
    REQUIRE(std::string(RuntimeConfig::TEST_CONFIG_PATH) == "config/settings-test.json");
}
