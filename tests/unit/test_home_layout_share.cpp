// Copyright (C) 2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "home_layout_share.h"

#include "../catch_amalgamated.hpp"
#include "hv/json.hpp"

using namespace helix::home_layout;
using json = nlohmann::json;

namespace {

json entry(const std::string& id, json config = nullptr) {
    json e = {{"id", id}, {"enabled", true}, {"col", 0},
              {"row", 0}, {"colspan", 1},    {"rowspan", 1}};
    if (!config.is_null()) {
        e["config"] = std::move(config);
    }
    return e;
}

json page(int id, std::initializer_list<json> widgets) {
    return {{"id", id}, {"widgets", json(widgets)}};
}

json serialized(std::initializer_list<json> pages, int main_index = 0) {
    return {{"pages", json(pages)},
            {"main_page_index", main_index},
            {"next_page_id", 9},
            {"default", {{"signature", "x"}, {"hash", "h"}}}};
}

json published(json pages_obj, const std::string& grid = "12x8") {
    return build_payload(pages_obj, grid, "1.1.0", 1700000000);
}

const json* find_entry(const json& pages_payload, size_t page_idx, const std::string& id) {
    for (const auto& w : pages_payload["pages"][page_idx]["widgets"]) {
        if (w["id"] == id) {
            return &w;
        }
    }
    return nullptr;
}

} // namespace

TEST_CASE("build_payload strips default and next_page_id", "[home_layout_share]") {
    const json p = published(serialized({page(1, {entry("host_health")})}, 0));
    REQUIRE(p["schema"] == kSchema);
    REQUIRE(p["grid"] == "12x8");
    REQUIRE(p["saved_at"] == 1700000000);
    REQUIRE(p["app_version"] == "1.1.0");
    REQUIRE(p["main_page_index"] == 0);
    REQUIRE(p["pages"].size() == 1);
    REQUIRE_FALSE(p.contains("default"));
    REQUIRE_FALSE(p.contains("next_page_id"));
    REQUIRE_FALSE(p["pages"].is_null());
}

TEST_CASE("round trip: parse_payload(build_payload(x)) keeps every printer-scoped entry and "
          "its config",
          "[home_layout_share]") {
    const json p =
        published(serialized({page(1, {entry("favorite_macro", {{"macro", "CLEAN_NOZZLE"}}),
                                       entry("preheat", {{"material_index", 2}})})},
                             0));
    const auto parsed = parse_payload(p);
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->cols == 12);
    REQUIRE(parsed->rows == 8);
    REQUIRE(parsed->pages_payload["pages"].size() == 1);
    REQUIRE(parsed->pages_payload["main_page_index"] == 0);
    const json* macro = find_entry(parsed->pages_payload, 0, "favorite_macro");
    REQUIRE(macro != nullptr);
    REQUIRE((*macro)["config"]["macro"] == "CLEAN_NOZZLE");
    const json* preheat = find_entry(parsed->pages_payload, 0, "preheat");
    REQUIRE(preheat != nullptr);
    REQUIRE((*preheat)["config"]["material_index"] == 2);
}

TEST_CASE("screen-scoped entries are removed, host_health kept", "[home_layout_share]") {
    const json p = published(serialized(
        {page(1, {entry("clock"), entry("host_health"), entry("network"), entry("tips")})}));
    const auto parsed = parse_payload(p);
    REQUIRE(parsed.has_value());
    const auto& widgets = parsed->pages_payload["pages"][0]["widgets"];
    REQUIRE(widgets.size() == 1);
    REQUIRE(widgets[0]["id"] == "host_health");
}

TEST_CASE("a page left empty is dropped and main_page_index follows its page",
          "[home_layout_share]") {
    const json p = published(serialized(
        {page(1, {entry("host_health")}), page(2, {entry("clock")}), page(3, {entry("led")})}, 2));
    const auto parsed = parse_payload(p);
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->pages_payload["pages"].size() == 2);
    REQUIRE(parsed->pages_payload["main_page_index"] == 1);
    REQUIRE(parsed->pages_payload["pages"][1]["id"] == 3);
}

TEST_CASE("parse_payload rejects malformed values", "[home_layout_share]") {
    const json good = published(serialized({page(1, {entry("host_health")})}));
    REQUIRE(parse_payload(good).has_value());

    auto with = [&](const char* key, json v) {
        json j = good;
        if (v.is_discarded()) {
            j.erase(key);
        } else {
            j[key] = std::move(v);
        }
        return j;
    };

    REQUIRE_FALSE(parse_payload(json::array()).has_value());
    REQUIRE_FALSE(parse_payload(json("text")).has_value());
    REQUIRE_FALSE(parse_payload(with("schema", 2)).has_value());
    REQUIRE_FALSE(parse_payload(with("schema", json::value_t::discarded)).has_value());
    REQUIRE_FALSE(parse_payload(with("grid", "0x8")).has_value());
    REQUIRE_FALSE(parse_payload(with("grid", "12x")).has_value());
    REQUIRE_FALSE(parse_payload(with("grid", "200x8")).has_value());
    REQUIRE_FALSE(parse_payload(with("pages", "x")).has_value());
    REQUIRE_FALSE(parse_payload(with("pages", json::array())).has_value());

    const json only_screen = published(serialized({page(1, {entry("clock"), entry("lock")})}));
    REQUIRE_FALSE(parse_payload(only_screen).has_value());
}

TEST_CASE("unknown widget ids and unknown config keys survive parsing", "[home_layout_share]") {
    const json p = published(serialized({page(
        1, {entry("plugin_gizmo", {{"anything", 1}}), entry("led", {{"future_key", "v"}})})}));
    const auto parsed = parse_payload(p);
    REQUIRE(parsed.has_value());
    const json* gizmo = find_entry(parsed->pages_payload, 0, "plugin_gizmo");
    REQUIRE(gizmo != nullptr);
    REQUIRE((*gizmo)["config"]["anything"] == 1);
    const json* led = find_entry(parsed->pages_payload, 0, "led");
    REQUIRE(led != nullptr);
    REQUIRE((*led)["config"]["future_key"] == "v");
}

TEST_CASE("strip_screen_scoped: main page dropped resets main_page_index to 0",
          "[home_layout_share]") {
    json pages = serialized(
        {page(1, {entry("host_health")}), page(2, {entry("led")}), page(3, {entry("clock")})}, 2);
    REQUIRE(strip_screen_scoped(pages));
    REQUIRE(pages["pages"].size() == 2);
    REQUIRE(pages["main_page_index"] == 0);
}

TEST_CASE("strip_screen_scoped: an earlier dropped page shifts main_page_index down",
          "[home_layout_share]") {
    json pages = serialized(
        {page(1, {entry("clock")}), page(2, {entry("led")}), page(3, {entry("host_health")})}, 2);
    REQUIRE(strip_screen_scoped(pages));
    REQUIRE(pages["pages"].size() == 2);
    REQUIRE(pages["main_page_index"] == 1);
    REQUIRE(pages["pages"][1]["id"] == 3);
}

TEST_CASE("strip_screen_scoped: every page screen-scoped returns false", "[home_layout_share]") {
    json pages = serialized({page(1, {entry("clock")}), page(2, {entry("network")})}, 1);
    REQUIRE_FALSE(strip_screen_scoped(pages));
}

TEST_CASE("strip_screen_scoped: payload without a pages array returns false",
          "[home_layout_share]") {
    json pages = {{"main_page_index", 0}};
    REQUIRE_FALSE(strip_screen_scoped(pages));
}
