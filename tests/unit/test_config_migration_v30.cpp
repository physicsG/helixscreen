// Copyright (C) 2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

// Exercises the v29 -> v30 config migration: each printer's lane eject distance
// and speed move to the backend-neutral ams/eject_* keys.

#include "config.h"
#include "config_migrations.h"

#include "../catch_amalgamated.hpp"
#include "hv/json.hpp"

using nlohmann::json;

namespace {

json migrate(json printers) {
    json config{{"config_version", 29}, {"printers", std::move(printers)}};
    helix::config_detail::run_versioned_migrations(config, "");
    REQUIRE(config["config_version"] == helix::kCurrentConfigVersion);
    return config["printers"];
}

} // namespace

TEST_CASE("v30 moves each printer's qidi_eject_* to eject_*", "[config][migration][eject]") {
    json printers =
        migrate({{"a", {{"ams", {{"qidi_eject_distance", 500}, {"qidi_eject_velocity", 60}}}}},
                 {"b", {{"ams", {{"qidi_eject_distance", 1200}}}}}});
    CHECK(printers["a"]["ams"]["eject_distance"] == 500);
    CHECK(printers["a"]["ams"]["eject_velocity"] == 60);
    CHECK_FALSE(printers["a"]["ams"].contains("qidi_eject_distance"));
    CHECK_FALSE(printers["a"]["ams"].contains("qidi_eject_velocity"));
    CHECK(printers["b"]["ams"]["eject_distance"] == 1200);
    CHECK_FALSE(printers["b"]["ams"].contains("eject_velocity"));
    CHECK_FALSE(printers["b"]["ams"].contains("qidi_eject_distance"));
}

TEST_CASE("v30 keeps a printer's existing eject_distance", "[config][migration][eject]") {
    json printers =
        migrate({{"a", {{"ams", {{"qidi_eject_distance", 500}, {"eject_distance", 900}}}}}});
    CHECK(printers["a"]["ams"]["eject_distance"] == 900);
    CHECK_FALSE(printers["a"]["ams"].contains("qidi_eject_distance"));
}

TEST_CASE("v30 leaves a printer without the keys untouched", "[config][migration][eject]") {
    json printers = migrate({{"a", {{"ams", {{"keep_spool_info_on_eject", true}}}}}});
    CHECK(printers["a"]["ams"] == json{{"keep_spool_info_on_eject", true}});
}
