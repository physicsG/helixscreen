// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_settings_template_contract.cpp
 * @brief Keeps config/settings.json.template honest about the schema it claims to document.
 *
 * The template is a self-documenting reference: DEVELOPMENT.md tells contributors
 * to `cp config/settings.json.template config/settings.json` for first-time setup,
 * and RuntimeConfig::PROD_CONFIG_PATH is the relative path "config/settings.json",
 * so that copy really is the config the app loads from a repo checkout.
 *
 * Nothing checked it, and it rotted into a state no migration could repair:
 *
 *  - No "config_version". Config::init treats a missing version as 0 and reads
 *    that as "a tarball default" (config.cpp), restoring a rolling backup over
 *    the loaded file if one exists — so the documented values silently lose.
 *  - A legacy singular "printer" object alongside a "printers" key that held only
 *    show_printer_switcher. normalize_versionless_document moves "printer" under "printers/<slug>"
 *    but returns early when "printers" already exists, so the whole printer block —
 *    host, port, every timeout — was orphaned and ignored, with no error.
 *  - "display.theme" naming a theme that is not DEFAULT_THEME, with a comment
 *    claiming it was both the default and the only option.
 *
 * These assertions pin each of those. They are deliberately about STRUCTURE the
 * loader depends on, not about the full key set: the template documenting more
 * optional settings than a fresh config writes is the file doing its job, so this
 * never asserts the template is a subset of anything.
 *
 * If you add a migration, CURRENT_CONFIG_VERSION moves and this fails until the
 * template's config_version follows. That is the point — it is the only thing
 * stopping this file from silently rotting again.
 */

#include "config.h"
#include "theme_loader.h"

#include <fstream>
#include <string>

#include "../catch_amalgamated.hpp"
#include "hv/json.hpp"

namespace {

/// Resolve the repo root from __FILE__ so the test does not depend on cwd.
std::string repo_root() {
    std::string src = __FILE__;
    auto pos = src.rfind("/tests/unit/");
    if (pos != std::string::npos) {
        return src.substr(0, pos);
    }
    return ".";
}

nlohmann::json load_template() {
    const std::string path = repo_root() + "/config/settings.json.template";
    std::ifstream f(path);
    INFO("template missing or unreadable: " << path);
    REQUIRE(f.is_open());
    // Parse non-throwing so a malformed template reports as a clean failure
    // rather than an exception escaping the test binary.
    auto data = nlohmann::json::parse(f, nullptr, false);
    INFO("template is not valid JSON: " << path);
    REQUIRE_FALSE(data.is_discarded());
    return data;
}

bool theme_file_exists(const std::string& name) {
    const std::string path = repo_root() + "/assets/config/themes/defaults/" + name + ".json";
    std::ifstream f(path);
    return f.is_open();
}

} // namespace

TEST_CASE("settings template declares the current config version", "[config][template]") {
    auto tpl = load_template();

    INFO("A missing/stale config_version makes Config::init treat the template as a "
         "tarball default and restore a backup over it, so nothing in the file applies.");
    REQUIRE(tpl.contains("config_version"));
    REQUIRE(tpl["config_version"].is_number_integer());
    CHECK(tpl["config_version"].get<int>() == helix::CURRENT_CONFIG_VERSION);
}

TEST_CASE("settings template uses the multi-printer schema", "[config][template]") {
    auto tpl = load_template();

    INFO(
        "normalize_versionless_document returns early when 'printers' exists, so a legacy singular "
        "'printer' object alongside it can never migrate and is silently ignored.");
    CHECK_FALSE(tpl.contains("printer"));

    REQUIRE(tpl.contains("printers"));
    REQUIRE(tpl["printers"].is_object());

    REQUIRE(tpl.contains("active_printer_id"));
    REQUIRE(tpl["active_printer_id"].is_string());

    const auto active = tpl["active_printer_id"].get<std::string>();
    INFO("active_printer_id points at '" << active << "', which must exist under printers/");
    REQUIRE(tpl["printers"].contains(active));
    REQUIRE(tpl["printers"][active].is_object());
}

TEST_CASE("settings template does not ship a foreign printer address", "[config][template]") {
    auto tpl = load_template();
    REQUIRE(tpl.contains("active_printer_id"));
    const auto active = tpl["active_printer_id"].get<std::string>();
    REQUIRE(tpl["printers"].contains(active));

    const auto& printer = tpl["printers"][active];
    REQUIRE(printer.contains("moonraker_host"));
    const auto host = printer["moonraker_host"].get<std::string>();

    INFO("moonraker_host is '" << host
                               << "'. A contributor copying this template gets it verbatim, so it "
                                  "must be a loopback default rather than any real LAN address.");
    CHECK((host == "127.0.0.1" || host == "localhost"));
}

TEST_CASE("settings template names a theme that exists", "[config][template]") {
    auto tpl = load_template();
    REQUIRE(tpl.contains("display"));
    REQUIRE(tpl["display"].is_object());
    REQUIRE(tpl["display"].contains("theme"));

    const auto theme = tpl["display"]["theme"].get<std::string>();

    // Scoped so each message only accompanies its own assertion — an INFO lives to
    // the end of its enclosing scope, and a stray "theme file does not exist" printed
    // against the DEFAULT_THEME mismatch below would be an outright false statement.
    {
        INFO("display.theme is '" << theme << "' but assets/config/themes/defaults/" << theme
                                  << ".json does not exist");
        CHECK(theme_file_exists(theme));
    }
    {
        INFO("the template should ship the app's own default theme, DEFAULT_THEME = "
             << helix::DEFAULT_THEME);
        CHECK(theme == helix::DEFAULT_THEME);
    }
}
