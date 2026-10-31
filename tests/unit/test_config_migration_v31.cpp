// Copyright (C) 2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

// Exercises the v30 -> v31 config migration: screen sharing moves from the root
// into the active printer only.

#include "config.h"
#include "config_migrations.h"

#include "../catch_amalgamated.hpp"
#include "hv/json.hpp"

using nlohmann::json;

namespace {

json migrate(json config) {
    config["config_version"] = 30;
    helix::config_detail::run_versioned_migrations(config, "");
    REQUIRE(config["config_version"] == helix::kCurrentConfigVersion);
    return config;
}

} // namespace

TEST_CASE("v31 gives sharing to the active printer only", "[config][migration][remote_screen]") {
    json config = migrate(
        {{"active_printer_id", "b"},
         {"remote_screen", {{"enabled", true}, {"control", true}, {"port", 7130}}},
         {"printers",
          {{"show_printer_switcher", true}, {"a", json::object()}, {"b", json::object()}}}});
    CHECK(config["printers"]["b"]["remote_screen"]["enabled"] == true);
    CHECK(config["printers"]["b"]["remote_screen"]["control"] == true);
    CHECK_FALSE(config["printers"]["a"].contains("remote_screen"));
    CHECK_FALSE(config["remote_screen"].contains("enabled"));
    CHECK_FALSE(config["remote_screen"].contains("control"));
    CHECK(config["remote_screen"]["port"] == 7130);
}

TEST_CASE("v31 keeps a printer's own sharing value", "[config][migration][remote_screen]") {
    json config = migrate({{"active_printer_id", "a"},
                           {"remote_screen", {{"enabled", true}}},
                           {"printers", {{"a", {{"remote_screen", {{"enabled", false}}}}}}}});
    CHECK(config["printers"]["a"]["remote_screen"]["enabled"] == false);
    CHECK_FALSE(config["remote_screen"].contains("enabled"));
}

TEST_CASE("v31 leaves a config with no printer alone", "[config][migration][remote_screen]") {
    json config = migrate({{"remote_screen", {{"enabled", true}}}});
    CHECK(config["remote_screen"]["enabled"] == true);
}
