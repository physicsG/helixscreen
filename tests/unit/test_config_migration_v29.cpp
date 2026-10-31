// Copyright (C) 2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

// Exercises the v28 -> v29 config migration: each printer's unload-after-print
// choice moves to the backend-neutral ams/unload_after_print key.

#include "config.h"
#include "config_migrations.h"

#include "../catch_amalgamated.hpp"
#include "hv/json.hpp"

using nlohmann::json;

namespace {

json migrate(json printers) {
    json config{{"config_version", 28}, {"printers", std::move(printers)}};
    helix::config_detail::run_versioned_migrations(config, "");
    REQUIRE(config["config_version"] == helix::kCurrentConfigVersion);
    return config["printers"];
}

} // namespace

TEST_CASE("v29 moves each printer's afc_unload_after_print to unload_after_print",
          "[config][migration][unload_after_print]") {
    json printers = migrate({{"a", {{"ams", {{"afc_unload_after_print", true}}}}},
                             {"b", {{"ams", {{"afc_unload_after_print", false}}}}}});
    CHECK(printers["a"]["ams"]["unload_after_print"] == true);
    CHECK_FALSE(printers["a"]["ams"].contains("afc_unload_after_print"));
    CHECK(printers["b"]["ams"]["unload_after_print"] == false);
    CHECK_FALSE(printers["b"]["ams"].contains("afc_unload_after_print"));
}

TEST_CASE("v29 keeps a printer's existing unload_after_print",
          "[config][migration][unload_after_print]") {
    json printers = migrate(
        {{"a", {{"ams", {{"afc_unload_after_print", false}, {"unload_after_print", true}}}}}});
    CHECK(printers["a"]["ams"]["unload_after_print"] == true);
    CHECK_FALSE(printers["a"]["ams"].contains("afc_unload_after_print"));
}

TEST_CASE("v29 leaves a printer without the key untouched",
          "[config][migration][unload_after_print]") {
    json printers = migrate({{"a", {{"ams", {{"keep_spool_info_on_eject", true}}}}}});
    CHECK(printers["a"]["ams"] == json{{"keep_spool_info_on_eject", true}});
}
