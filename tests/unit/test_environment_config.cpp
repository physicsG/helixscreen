// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../test_helpers/scoped_env.h"
#include "env_knobs.h"
#include "environment_config.h"

#include <cstdlib>

#include "../catch_amalgamated.hpp"

using namespace helix;
using namespace helix::config;

TEST_CASE("EnvironmentConfig::get_int basic parsing", "[environment][config]") {
    SECTION("Valid integer within range") {
        ScopedEnv guard("TEST_INT_VAR", "42");
        auto result = EnvironmentConfig::get_int("TEST_INT_VAR", 0, 100);
        REQUIRE(result.has_value());
        REQUIRE(*result == 42);
    }

    SECTION("Integer at minimum bound") {
        ScopedEnv guard("TEST_INT_VAR", "0");
        auto result = EnvironmentConfig::get_int("TEST_INT_VAR", 0, 100);
        REQUIRE(result.has_value());
        REQUIRE(*result == 0);
    }

    SECTION("Integer at maximum bound") {
        ScopedEnv guard("TEST_INT_VAR", "100");
        auto result = EnvironmentConfig::get_int("TEST_INT_VAR", 0, 100);
        REQUIRE(result.has_value());
        REQUIRE(*result == 100);
    }

    SECTION("Integer below minimum returns nullopt") {
        ScopedEnv guard("TEST_INT_VAR", "-1");
        auto result = EnvironmentConfig::get_int("TEST_INT_VAR", 0, 100);
        REQUIRE_FALSE(result.has_value());
    }

    SECTION("Integer above maximum returns nullopt") {
        ScopedEnv guard("TEST_INT_VAR", "101");
        auto result = EnvironmentConfig::get_int("TEST_INT_VAR", 0, 100);
        REQUIRE_FALSE(result.has_value());
    }

    SECTION("Missing variable returns nullopt") {
        ScopedEnv guard("TEST_INT_VAR", nullptr); // unset
        auto result = EnvironmentConfig::get_int("TEST_INT_VAR", 0, 100);
        REQUIRE_FALSE(result.has_value());
    }

    SECTION("Invalid non-numeric string returns nullopt") {
        ScopedEnv guard("TEST_INT_VAR", "abc");
        auto result = EnvironmentConfig::get_int("TEST_INT_VAR", 0, 100);
        REQUIRE_FALSE(result.has_value());
    }

    SECTION("Partial numeric string returns nullopt") {
        ScopedEnv guard("TEST_INT_VAR", "42abc");
        auto result = EnvironmentConfig::get_int("TEST_INT_VAR", 0, 100);
        REQUIRE_FALSE(result.has_value());
    }

    SECTION("Empty string returns nullopt") {
        ScopedEnv guard("TEST_INT_VAR", "");
        auto result = EnvironmentConfig::get_int("TEST_INT_VAR", 0, 100);
        REQUIRE_FALSE(result.has_value());
    }

    SECTION("Negative values work with negative range") {
        ScopedEnv guard("TEST_INT_VAR", "-50");
        auto result = EnvironmentConfig::get_int("TEST_INT_VAR", -100, 0);
        REQUIRE(result.has_value());
        REQUIRE(*result == -50);
    }
}

TEST_CASE("EnvironmentConfig::get_int_scaled", "[environment][config]") {
    SECTION("Scales value by divisor") {
        ScopedEnv guard("TEST_MS_VAR", "5000");
        // 5000ms -> 5 seconds (divisor=1000)
        auto result = EnvironmentConfig::get_int_scaled("TEST_MS_VAR", 1, 60, 1000);
        REQUIRE(result.has_value());
        REQUIRE(*result == 5);
    }

    SECTION("Rounds up fractional results") {
        ScopedEnv guard("TEST_MS_VAR", "5500");
        // 5500ms -> ceil(5.5) = 6 seconds
        auto result = EnvironmentConfig::get_int_scaled("TEST_MS_VAR", 1, 60, 1000);
        REQUIRE(result.has_value());
        REQUIRE(*result == 6);
    }

    SECTION("Validates scaled result against range") {
        ScopedEnv guard("TEST_MS_VAR", "500");
        // 500ms -> 1 second, but min is 2
        auto result = EnvironmentConfig::get_int_scaled("TEST_MS_VAR", 2, 60, 1000);
        REQUIRE_FALSE(result.has_value());
    }

    SECTION("Zero divisor returns nullopt") {
        ScopedEnv guard("TEST_MS_VAR", "5000");
        auto result = EnvironmentConfig::get_int_scaled("TEST_MS_VAR", 1, 60, 0);
        REQUIRE_FALSE(result.has_value());
    }

    SECTION("Negative divisor returns nullopt") {
        ScopedEnv guard("TEST_MS_VAR", "5000");
        auto result = EnvironmentConfig::get_int_scaled("TEST_MS_VAR", 1, 60, -1);
        REQUIRE_FALSE(result.has_value());
    }
}

TEST_CASE("env_flag reads a variable through env_truthy", "[environment][config][env]") {
    SECTION("\"1\", \"true\", \"Yes\" and \" on \" return true") {
        for (const char* v : {"1", "true", "Yes", " on "}) {
            ScopedEnv guard("TEST_BOOL_VAR", v);
            CHECK(helix::env_flag("TEST_BOOL_VAR"));
        }
    }

    SECTION("\"0\", \"false\", \"10\" and \"1abc\" return false") {
        for (const char* v : {"0", "false", "10", "1abc"}) {
            ScopedEnv guard("TEST_BOOL_VAR", v);
            CHECK_FALSE(helix::env_flag("TEST_BOOL_VAR"));
        }
    }

    SECTION("Missing variable returns false") {
        ScopedEnv guard("TEST_BOOL_VAR", nullptr); // unset
        REQUIRE_FALSE(helix::env_flag("TEST_BOOL_VAR"));
    }

    SECTION("Empty string returns false") {
        ScopedEnv guard("TEST_BOOL_VAR", "");
        REQUIRE_FALSE(helix::env_flag("TEST_BOOL_VAR"));
    }
}

TEST_CASE("EnvironmentConfig::exists", "[environment][config]") {
    SECTION("Returns true for existing variable") {
        ScopedEnv guard("TEST_EXISTS_VAR", "anything");
        REQUIRE(EnvironmentConfig::exists("TEST_EXISTS_VAR") == true);
    }

    SECTION("Returns true for empty variable") {
        ScopedEnv guard("TEST_EXISTS_VAR", "");
        // Empty string still means the variable exists
        REQUIRE(EnvironmentConfig::exists("TEST_EXISTS_VAR") == true);
    }

    SECTION("Returns false for missing variable") {
        ScopedEnv guard("TEST_EXISTS_VAR", nullptr); // unset
        REQUIRE(EnvironmentConfig::exists("TEST_EXISTS_VAR") == false);
    }
}

TEST_CASE("EnvironmentConfig::get_string", "[environment][config]") {
    SECTION("Returns value for existing variable") {
        ScopedEnv guard("TEST_STR_VAR", "hello");
        auto result = EnvironmentConfig::get_string("TEST_STR_VAR");
        REQUIRE(result.has_value());
        REQUIRE(*result == "hello");
    }

    SECTION("Returns empty string for empty variable") {
        ScopedEnv guard("TEST_STR_VAR", "");
        auto result = EnvironmentConfig::get_string("TEST_STR_VAR");
        REQUIRE(result.has_value());
        REQUIRE(*result == "");
    }

    SECTION("Returns nullopt for missing variable") {
        ScopedEnv guard("TEST_STR_VAR", nullptr); // unset
        auto result = EnvironmentConfig::get_string("TEST_STR_VAR");
        REQUIRE_FALSE(result.has_value());
    }
}

// ============================================================================
// Application-specific helpers (HELIX_* environment variables)
// ============================================================================

TEST_CASE("EnvironmentConfig::get_auto_quit_seconds", "[environment][config][helix]") {
    SECTION("Converts milliseconds to seconds with ceiling") {
        ScopedEnv guard("HELIX_AUTO_QUIT_MS", "5000");
        auto result = EnvironmentConfig::get_auto_quit_seconds();
        REQUIRE(result.has_value());
        REQUIRE(*result == 5);
    }

    SECTION("Rounds up fractional seconds") {
        ScopedEnv guard("HELIX_AUTO_QUIT_MS", "5500");
        auto result = EnvironmentConfig::get_auto_quit_seconds();
        REQUIRE(result.has_value());
        REQUIRE(*result == 6);
    }

    SECTION("Rejects values below minimum (100ms)") {
        ScopedEnv guard("HELIX_AUTO_QUIT_MS", "50");
        auto result = EnvironmentConfig::get_auto_quit_seconds();
        REQUIRE_FALSE(result.has_value());
    }

    SECTION("Rejects values above maximum (3600000ms = 1hr)") {
        ScopedEnv guard("HELIX_AUTO_QUIT_MS", "4000000");
        auto result = EnvironmentConfig::get_auto_quit_seconds();
        REQUIRE_FALSE(result.has_value());
    }

    SECTION("Returns nullopt when not set") {
        ScopedEnv guard("HELIX_AUTO_QUIT_MS", nullptr); // unset
        auto result = EnvironmentConfig::get_auto_quit_seconds();
        REQUIRE_FALSE(result.has_value());
    }
}

TEST_CASE("EnvironmentConfig::get_screenshot_enabled", "[environment][config][helix]") {
    SECTION("Returns true when HELIX_AUTO_SCREENSHOT=1") {
        ScopedEnv guard("HELIX_AUTO_SCREENSHOT", "1");
        REQUIRE(EnvironmentConfig::get_screenshot_enabled() == true);
    }

    SECTION("Returns false when HELIX_AUTO_SCREENSHOT=0") {
        ScopedEnv guard("HELIX_AUTO_SCREENSHOT", "0");
        REQUIRE(EnvironmentConfig::get_screenshot_enabled() == false);
    }

    SECTION("Returns false when not set") {
        ScopedEnv guard("HELIX_AUTO_SCREENSHOT", nullptr); // unset
        REQUIRE(EnvironmentConfig::get_screenshot_enabled() == false);
    }
}

TEST_CASE("EnvironmentConfig::get_mock_ams_gates", "[environment][config][helix]") {
    SECTION("Returns valid gate count") {
        ScopedEnv guard("HELIX_AMS_GATES", "4");
        auto result = EnvironmentConfig::get_mock_ams_gates();
        REQUIRE(result.has_value());
        REQUIRE(*result == 4);
    }

    SECTION("Accepts minimum (1 gate)") {
        ScopedEnv guard("HELIX_AMS_GATES", "1");
        auto result = EnvironmentConfig::get_mock_ams_gates();
        REQUIRE(result.has_value());
        REQUIRE(*result == 1);
    }

    SECTION("Accepts maximum (16 gates)") {
        ScopedEnv guard("HELIX_AMS_GATES", "16");
        auto result = EnvironmentConfig::get_mock_ams_gates();
        REQUIRE(result.has_value());
        REQUIRE(*result == 16);
    }

    SECTION("Rejects 0 gates") {
        ScopedEnv guard("HELIX_AMS_GATES", "0");
        auto result = EnvironmentConfig::get_mock_ams_gates();
        REQUIRE_FALSE(result.has_value());
    }

    SECTION("Rejects > 16 gates") {
        ScopedEnv guard("HELIX_AMS_GATES", "17");
        auto result = EnvironmentConfig::get_mock_ams_gates();
        REQUIRE_FALSE(result.has_value());
    }

    SECTION("Returns nullopt when not set") {
        ScopedEnv guard("HELIX_AMS_GATES", nullptr); // unset
        auto result = EnvironmentConfig::get_mock_ams_gates();
        REQUIRE_FALSE(result.has_value());
    }
}

TEST_CASE("EnvironmentConfig::get_benchmark_mode", "[environment][config][helix]") {
    SECTION("Returns true when HELIX_BENCHMARK exists") {
        ScopedEnv guard("HELIX_BENCHMARK", "1");
        REQUIRE(EnvironmentConfig::get_benchmark_mode() == true);
    }

    SECTION("Returns true even with empty value") {
        ScopedEnv guard("HELIX_BENCHMARK", "");
        REQUIRE(EnvironmentConfig::get_benchmark_mode() == true);
    }

    SECTION("Returns false when not set") {
        ScopedEnv guard("HELIX_BENCHMARK", nullptr); // unset
        REQUIRE(EnvironmentConfig::get_benchmark_mode() == false);
    }
}
