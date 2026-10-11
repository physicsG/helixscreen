// Copyright (C) 2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

// TEST_MIRROR_OK: the code under test is itself a test helper - ConfigDirGuard
// in tests/test_helpers/ - so there is no include/ or src/ header to pull in.
// The case runs the real guard and reads the live environment variable.

#include "../test_helpers/config_dir_guard.h"
#include "../test_helpers/scoped_env.h"

#include <cstdlib>
#include <filesystem>
#include <string>

#include "../catch_amalgamated.hpp"

// The guard's env-var contract, pinned by reading HELIX_CONFIG_DIR mid-life.
// An empty value does not mean "no config": it isolates the lock and socket
// but Config still bootstraps a printer address from the user's settings
// backup, so a guard publishing an empty path silently aims a test run at a
// real printer. Tests that only read files THROUGH the variable would stay
// green while that happens; this file reads the variable itself.
TEST_CASE("ConfigDirGuard publishes its directory and restores what was there",
          "[config][isolation]") {
    // Previously set: the exact prior value comes back, not an unset.
    {
        helix::ScopedEnv seed("HELIX_CONFIG_DIR", "/sentinel/config/dir");
        {
            helix::ConfigDirGuard guard("env_contract_set");
            const char* during = std::getenv("HELIX_CONFIG_DIR");
            REQUIRE(during != nullptr);
            CHECK(std::string(during) == guard.dir.string());
            CHECK_FALSE(std::string(during).empty());
            CHECK(std::filesystem::is_directory(guard.dir));
        }
        const char* after = std::getenv("HELIX_CONFIG_DIR");
        REQUIRE(after != nullptr);
        CHECK(std::string(after) == "/sentinel/config/dir");
    }

    // Previously unset: the guard unsets again rather than leaving its own
    // directory behind for every later case in the shard.
    {
        helix::ScopedEnv seed("HELIX_CONFIG_DIR", nullptr);
        REQUIRE(std::getenv("HELIX_CONFIG_DIR") == nullptr);
        {
            helix::ConfigDirGuard guard("env_contract_unset");
            const char* during = std::getenv("HELIX_CONFIG_DIR");
            REQUIRE(during != nullptr);
            CHECK(std::string(during) == guard.dir.string());
        }
        CHECK(std::getenv("HELIX_CONFIG_DIR") == nullptr);
    }
}
