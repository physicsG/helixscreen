// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

// Exercises what run_versioned_migrations() does with a config_version NEWER
// than this build understands.
//
// This became reachable the moment update channels are user-switchable: moving
// from the devel channel back to stable installs an OLDER binary on top of a
// config the newer build already migrated. Every migration gate is `version <
// N`, so none of them fire — but the stamp at the end of the chain is
// unconditional, so the older build used to rewrite config_version DOWN to its
// own. The newer build would then re-run migrations it had already applied,
// against data already in the new shape.
//
// The contract pinned here: a future config is left entirely alone — not
// migrated, not stamped, and its unknown keys survive a save round trip through
// the older build.
//
// Driven through the public Config::init() path (the migration runner is a
// static function in config.cpp), same as the v18/v21 migration tests.

#include "config.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "../catch_amalgamated.hpp"

namespace fs = std::filesystem;
using namespace helix;

namespace {

class MigrationFutureFixture {
  protected:
    Config config;
    std::string temp_dir;
    std::string config_path;
    std::string saved_config_dir_;
    bool had_config_dir_ = false;

    void SetUp() {
        temp_dir = (fs::temp_directory_path() / "helix_migration_future_test").string();
        fs::remove_all(temp_dir);
        fs::create_directories(temp_dir);

        if (const char* prev = std::getenv("HELIX_CONFIG_DIR")) {
            saved_config_dir_ = prev;
            had_config_dir_ = true;
        }
        setenv("HELIX_CONFIG_DIR", temp_dir.c_str(), 1);

        config_path = temp_dir + "/settings.json";
    }

    void TearDown() {
        fs::remove_all(temp_dir);
        if (had_config_dir_) {
            setenv("HELIX_CONFIG_DIR", saved_config_dir_.c_str(), 1);
        } else {
            unsetenv("HELIX_CONFIG_DIR");
        }
        config.clear_path();
    }

    void write_and_init(const json& contents) {
        std::ofstream f(config_path);
        f << contents.dump(2);
        f.close();
        config.init(config_path);
    }

    /// Read the on-disk document directly, bypassing Config's accessors, so a
    /// test can prove what was actually persisted.
    json read_raw() const {
        std::ifstream f(config_path);
        return json::parse(f);
    }

  public:
    MigrationFutureFixture() {
        SetUp();
    }
    ~MigrationFutureFixture() {
        TearDown();
    }
};

/// A config as a NEWER build would have written it: a version this build has
/// never heard of, plus a settings block introduced after this build shipped.
json future_config(int version) {
    return json{{"config_version", version},
                {"active_printer_id", "voronv2"},
                {"appearance", {{"show_widget_labels", true}}},
                // A key this build knows nothing about — stands in for whatever
                // the devel track added.
                {"feature_from_the_future", {{"enabled", true}, {"threshold", 42}}},
                {"printers", {{"voronv2", {{"moonraker_host", "192.168.1.112"}}}}}};
}

} // namespace

// ============================================================================
// A future config is left unstamped
// ============================================================================

TEST_CASE_METHOD(MigrationFutureFixture,
                 "Config migration: a newer config_version is not stamped down",
                 "[config][migration]") {
    const int future = CURRENT_CONFIG_VERSION + 1;
    write_and_init(future_config(future));

    // The load must not rewrite the version to ours. Stamping it down is what
    // makes the newer build re-run already-applied migrations on its next boot.
    CHECK(config.get<int>("/config_version", -1) == future);
    CHECK(config.get<int>("/config_version", -1) != CURRENT_CONFIG_VERSION);
}

TEST_CASE_METHOD(MigrationFutureFixture,
                 "Config migration: a far-future config_version is still not stamped down",
                 "[config][migration]") {
    // Not just off-by-one: several devel releases' worth of drift.
    const int future = CURRENT_CONFIG_VERSION + 7;
    write_and_init(future_config(future));

    CHECK(config.get<int>("/config_version", -1) == future);
}

// ============================================================================
// A future config's unknown keys survive a round trip
// ============================================================================

TEST_CASE_METHOD(MigrationFutureFixture,
                 "Config migration: unknown keys from a newer build survive a save",
                 "[config][migration]") {
    const int future = CURRENT_CONFIG_VERSION + 1;
    write_and_init(future_config(future));

    // The older build reads and writes settings it knows about...
    config.set<bool>("/appearance/show_widget_labels", false);
    REQUIRE(config.save());

    // ...and the block it has never heard of is still on disk afterwards. If
    // this fails, switching channels back and forth silently destroys settings.
    json raw = read_raw();
    REQUIRE(raw.contains("feature_from_the_future"));
    CHECK(raw["feature_from_the_future"]["enabled"] == true);
    CHECK(raw["feature_from_the_future"]["threshold"] == 42);

    // And the version is still the future one, not ours.
    CHECK(raw["config_version"] == future);

    // The known key really was written (proves the save actually happened and
    // the assertions above are not passing against an untouched file).
    CHECK(raw["appearance"]["show_widget_labels"] == false);
}

// ============================================================================
// Regression guard: the ordinary path still migrates
// ============================================================================

TEST_CASE_METHOD(MigrationFutureFixture,
                 "Config migration: an older config_version is still migrated and stamped",
                 "[config][migration]") {
    // The future-version early return must not swallow the normal case.
    json old = future_config(CURRENT_CONFIG_VERSION - 1);
    old.erase("feature_from_the_future");
    write_and_init(old);

    CHECK(config.get<int>("/config_version", -1) == CURRENT_CONFIG_VERSION);
}

TEST_CASE_METHOD(MigrationFutureFixture,
                 "Config migration: a config already at the current version is stamped unchanged",
                 "[config][migration]") {
    json current = future_config(CURRENT_CONFIG_VERSION);
    current.erase("feature_from_the_future");
    write_and_init(current);

    CHECK(config.get<int>("/config_version", -1) == CURRENT_CONFIG_VERSION);
}
