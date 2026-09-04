// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_spoolman_setup.h"

#include <string>

#include "../catch_amalgamated.hpp"

using helix::ui::SpoolmanSetup;

TEST_CASE("SpoolmanSetup::validate_port", "[spoolman_setup]") {
    SECTION("valid ports") {
        CHECK(SpoolmanSetup::validate_port("7912"));
        CHECK(SpoolmanSetup::validate_port("1"));
        CHECK(SpoolmanSetup::validate_port("65535"));
        CHECK(SpoolmanSetup::validate_port("80"));
        CHECK(SpoolmanSetup::validate_port("443"));
        CHECK(SpoolmanSetup::validate_port("8080"));
    }

    SECTION("invalid ports") {
        CHECK_FALSE(SpoolmanSetup::validate_port(""));
        CHECK_FALSE(SpoolmanSetup::validate_port("0"));
        CHECK_FALSE(SpoolmanSetup::validate_port("65536"));
        CHECK_FALSE(SpoolmanSetup::validate_port("abc"));
        CHECK_FALSE(SpoolmanSetup::validate_port("-1"));
        CHECK_FALSE(SpoolmanSetup::validate_port("99999"));
        CHECK_FALSE(SpoolmanSetup::validate_port("7912abc"));
        CHECK_FALSE(SpoolmanSetup::validate_port(" 7912"));
    }
}

TEST_CASE("SpoolmanSetup::validate_host", "[spoolman_setup]") {
    SECTION("valid hosts") {
        CHECK(SpoolmanSetup::validate_host("192.168.1.100"));
        CHECK(SpoolmanSetup::validate_host("spoolman.local"));
        CHECK(SpoolmanSetup::validate_host("my-server"));
        CHECK(SpoolmanSetup::validate_host("localhost"));
        CHECK(SpoolmanSetup::validate_host("10.0.0.1"));
    }

    SECTION("invalid hosts") {
        CHECK_FALSE(SpoolmanSetup::validate_host(""));
        CHECK_FALSE(SpoolmanSetup::validate_host("   "));
        CHECK_FALSE(SpoolmanSetup::validate_host("\t"));
    }
}

TEST_CASE("SpoolmanSetup::build_url", "[spoolman_setup]") {
    SECTION("basic URL construction") {
        CHECK(SpoolmanSetup::build_url("192.168.1.100", "7912") == "http://192.168.1.100:7912");
        CHECK(SpoolmanSetup::build_url("spoolman.local", "80") == "http://spoolman.local:80");
        CHECK(SpoolmanSetup::build_url("localhost", "7912") == "http://localhost:7912");
    }

    SECTION("uses default port constant") {
        std::string url = SpoolmanSetup::build_url("myhost", helix::ui::DEFAULT_SPOOLMAN_PORT);
        CHECK(url == "http://myhost:7912");
    }
}

TEST_CASE("SpoolmanSetup::build_probe_url", "[spoolman_setup]") {
    SECTION("appends health endpoint") {
        CHECK(SpoolmanSetup::build_probe_url("192.168.1.100", "7912") ==
              "http://192.168.1.100:7912/api/v1/health");
        CHECK(SpoolmanSetup::build_probe_url("spoolman.local", "80") ==
              "http://spoolman.local:80/api/v1/health");
    }
}

TEST_CASE("SpoolmanSetup::build_spoolman_config_entries", "[spoolman_setup]") {
    SECTION("returns server entry") {
        auto entries = SpoolmanSetup::build_spoolman_config_entries("192.168.1.100", "7912");
        REQUIRE(entries.size() == 1);
        CHECK(entries[0].first == "server");
        CHECK(entries[0].second == "http://192.168.1.100:7912");
    }

    SECTION("config entry URL matches build_url") {
        std::string host = "spoolman.local";
        std::string port = "8080";
        auto entries = SpoolmanSetup::build_spoolman_config_entries(host, port);
        REQUIRE_FALSE(entries.empty());
        CHECK(entries[0].second == SpoolmanSetup::build_url(host, port));
    }
}

TEST_CASE("SpoolmanSetup::parse_url_components", "[spoolman_setup]") {
    SECTION("extracts host and port from full URL") {
        auto [host, port] = SpoolmanSetup::parse_url_components("http://192.168.1.100:7912");
        CHECK(host == "192.168.1.100");
        CHECK(port == "7912");
    }

    SECTION("extracts host and port with non-default port") {
        auto [host, port] = SpoolmanSetup::parse_url_components("http://spoolman.local:8080");
        CHECK(host == "spoolman.local");
        CHECK(port == "8080");
    }

    SECTION("handles missing port, defaults to 7912") {
        auto [host, port] = SpoolmanSetup::parse_url_components("http://spoolman.local");
        CHECK(host == "spoolman.local");
        CHECK(port == "7912");
    }

    SECTION("strips trailing path") {
        auto [host, port] =
            SpoolmanSetup::parse_url_components("http://192.168.1.100:7912/api/v1/health");
        CHECK(host == "192.168.1.100");
        CHECK(port == "7912");
    }

    SECTION("empty URL returns empty host and default port") {
        auto [host, port] = SpoolmanSetup::parse_url_components("");
        CHECK(host == "");
        CHECK(port == "7912");
    }

    SECTION("round-trip: build_url then parse_url_components") {
        std::string orig_host = "192.168.1.100";
        std::string orig_port = "7912";
        std::string url = SpoolmanSetup::build_url(orig_host, orig_port);
        auto [host, port] = SpoolmanSetup::parse_url_components(url);
        CHECK(host == orig_host);
        CHECK(port == orig_port);
    }

    SECTION("round-trip with non-default port") {
        std::string orig_host = "spoolman.local";
        std::string orig_port = "8080";
        std::string url = SpoolmanSetup::build_url(orig_host, orig_port);
        auto [host, port] = SpoolmanSetup::parse_url_components(url);
        CHECK(host == orig_host);
        CHECK(port == orig_port);
    }

    SECTION("handles URL without scheme") {
        auto [host, port] = SpoolmanSetup::parse_url_components("192.168.1.100:7912");
        CHECK(host == "192.168.1.100");
        CHECK(port == "7912");
    }
}
