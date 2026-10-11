// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#if HELIX_HAS_PLUGINS

#include "plugin_consent.h"

#include <algorithm>
#include <string>
#include <vector>

#include "../catch_amalgamated.hpp"

using namespace helix::plugin;

TEST_CASE("consent names full control for gcode and config rewrites for moonraker_write",
          "[plugin][consent]") {
    auto lines = consent_lines({Permission::Gcode, Permission::MoonrakerWrite});
    REQUIRE(lines.size() == 2);
    CHECK(lines[0].find("full control") != std::string::npos);
    CHECK(lines[1].find("config") != std::string::npos);
    CHECK(lines[1].find("rewrit") != std::string::npos);
}

TEST_CASE("an update's consent lists only the new permissions", "[plugin][consent]") {
    Manifest m;
    m.name = "Orca";
    m.version = "1.2.0";
    m.permissions = {Permission::Gcode, Permission::Http};
    std::string msg = consent_message(m, {Permission::Http});
    CHECK(msg.find("new permissions") != std::string::npos);
    CHECK(msg.find("internet") != std::string::npos);
    CHECK(msg.find("full control") == std::string::npos);
}

TEST_CASE("no permissions says so", "[plugin][consent]") {
    Manifest m;
    m.name = "Plain";
    m.version = "1";
    CHECK(consent_message(m, {}).find("no special permissions") != std::string::npos);
}

TEST_CASE("every permission line is the approved wording verbatim", "[plugin][consent]") {
    auto lines = consent_lines(
        {Permission::Gcode, Permission::MoonrakerWrite, Permission::Http, Permission::Storage});
    REQUIRE(lines.size() == 4);
    CHECK(lines[0] == "Send any G-code command. This is full control of the printer: every "
                      "macro, including ones that run shell commands, is reachable.");
    CHECK(lines[1] == "Change printer settings through Moonraker, including uploading, "
                      "rewriting and deleting files in the printer's config folder.");
    CHECK(lines[2] == "Connect to servers on your network and the internet.");
    CHECK(lines[3] == "Keep its own data on this screen (up to 256 KB).");
}

TEST_CASE("a hostile-looking but valid name stays on the header line", "[plugin][consent]") {
    Manifest m;
    m.name = "Weather (asks for nothing, totally safe)";
    m.version = "1.0.0";
    m.permissions = {Permission::Gcode};
    std::string msg = consent_message(m, {});
    // Every newline comes from the permission lines; the name can never add one.
    CHECK(std::count(msg.begin(), msg.end(), '\n') == 1);
    CHECK(msg.substr(0, msg.find('\n')) == "Weather (asks for nothing, totally safe) 1.0.0");
    CHECK(msg.find("full control") != std::string::npos);
}

#endif // HELIX_HAS_PLUGINS
