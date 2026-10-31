// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

// Exercises the v27 -> v28 config migration: /display/screensaver_type holds a stable saver
// name, and a legacy integer becomes the name it stood for.

#include "config.h"
#include "config_migrations.h"
#include "screensaver_registry.h"

#include <iterator>

#include "../catch_amalgamated.hpp"
#include "hv/json.hpp"

using nlohmann::json;

namespace {

/// Runs the ladder over a v27 document whose display holds `stored` as screensaver_type.
json migrate(const json& stored) {
    json config{{"config_version", 27}, {"display", {{"screensaver_type", stored}}}};
    helix::config_detail::run_versioned_migrations(config, "");
    REQUIRE(config["config_version"] == helix::kCurrentConfigVersion);
    return config["display"]["screensaver_type"];
}

} // namespace

TEST_CASE("v28 turns a legacy screensaver type number into its name",
          "[config][migration][screensaver_setting]") {
    CHECK(migrate(0) == "off");
    CHECK(migrate(1) == "toasters");
    CHECK(migrate(7) == "maze");
    CHECK(migrate(8) == "hyperspace");
    CHECK(migrate(9) == "flipclock");
}

TEST_CASE("v28 writes the frozen table's name for every legacy number",
          "[config][migration][screensaver_setting]") {
    // The migration reads the frozen table alone; the live registry can lose or reorder a saver
    // without changing what an old number migrates to.
    for (size_t i = 0; i < std::size(helix::ui::kLegacyScreensaverTypeNames); i++) {
        CAPTURE(i);
        CHECK(migrate(static_cast<int>(i)) == helix::ui::kLegacyScreensaverTypeNames[i]);
    }
}

TEST_CASE("v28 turns an out-of-range screensaver number into the default saver",
          "[config][migration][screensaver_setting]") {
    CHECK(migrate(10) == "flipclock");
    CHECK(migrate(-3) == "flipclock");
}

TEST_CASE("v28 leaves a stored name and every other value as it is",
          "[config][migration][screensaver_setting]") {
    CHECK(migrate("maze") == "maze");
    CHECK(migrate("Hyperspace") == "Hyperspace");
    CHECK(migrate("lava lamp") == "lava lamp");
    CHECK(migrate(true) == true);
    CHECK(migrate(7.5) == 7.5);
}

TEST_CASE("v28 writes nothing when there is no screensaver setting",
          "[config][migration][screensaver_setting]") {
    json without_type{{"config_version", 27}, {"display", {{"sleep_sec", 600}}}};
    helix::config_detail::run_versioned_migrations(without_type, "");
    CHECK_FALSE(without_type["display"].contains("screensaver_type"));

    json without_display{{"config_version", 27}};
    helix::config_detail::run_versioned_migrations(without_display, "");
    CHECK_FALSE(without_display.contains("display"));
}

TEST_CASE("v28 replayed over a migrated document changes nothing",
          "[config][migration][screensaver_setting]") {
    // A rollback stamps config_version back down, so the ladder runs again over names.
    json config{{"config_version", 27}, {"display", {{"screensaver_type", 7}}}};
    helix::config_detail::run_versioned_migrations(config, "");
    config["config_version"] = 27;
    helix::config_detail::run_versioned_migrations(config, "");
    CHECK(config["display"]["screensaver_type"] == "maze");
}
