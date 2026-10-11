// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "screensaver_registry.h"

#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "../catch_amalgamated.hpp"

using helix::ui::SCREENSAVER_COUNT;
using helix::ui::SCREENSAVERS;

namespace {

/// Value of `attr` on the self-closing element whose name is `name`, with &#10; decoded to a
/// newline, or "" when there is no such element or attribute.
std::string attribute_of_named(const std::string& xml, const std::string& name,
                               const std::string& attr) {
    const size_t at = xml.find("name=\"" + name + "\"");
    if (at == std::string::npos) {
        return "";
    }
    const size_t open = xml.rfind('<', at);
    const size_t close = xml.find("/>", at);
    if (open == std::string::npos || close == std::string::npos) {
        return "";
    }
    const std::string element = xml.substr(open, close - open);
    const std::regex pattern("\\s" + attr + "=\"([^\"]*)\"");
    std::smatch match;
    if (!std::regex_search(element, match, pattern)) {
        return "";
    }
    std::string value = match[1].str();
    for (size_t pos = value.find("&#10;"); pos != std::string::npos;
         pos = value.find("&#10;", pos)) {
        value.replace(pos, 5, "\n");
        pos += 1;
    }
    return value;
}

std::vector<std::string> split_option_lines(const std::string& text) {
    std::vector<std::string> lines;
    std::stringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) {
        lines.push_back(line);
    }
    return lines;
}

} // namespace

TEST_CASE("screensaver registry rows run in type order from 1 with unique names",
          "[screensaver][screensaver_registry]") {
    REQUIRE(SCREENSAVER_COUNT > 0);
    std::set<std::string> names;
    for (size_t i = 0; i < SCREENSAVER_COUNT; i++) {
        CAPTURE(i);
        CHECK(static_cast<int>(SCREENSAVERS[i].type) == static_cast<int>(i) + 1);
        CHECK_FALSE(std::string(SCREENSAVERS[i].name).empty());
        CHECK_FALSE(std::string(SCREENSAVERS[i].label_key).empty());
        CHECK(SCREENSAVERS[i].depths != 0);
        names.insert(SCREENSAVERS[i].name);
    }
    CHECK(names.size() == SCREENSAVER_COUNT);
    CHECK(helix::ui::screensaver_last_type() == static_cast<int>(SCREENSAVER_COUNT));
}

TEST_CASE("an out-of-range screensaver type clamps to the last type",
          "[screensaver][screensaver_registry]") {
    const int last = static_cast<int>(SCREENSAVER_COUNT);
    CHECK(helix::ui::clamp_screensaver_type(-1) == 0);
    CHECK(helix::ui::clamp_screensaver_type(0) == 0);
    CHECK(helix::ui::clamp_screensaver_type(1) == 1);
    CHECK(helix::ui::clamp_screensaver_type(last) == last);
    CHECK(helix::ui::clamp_screensaver_type(last + 1) == last);
    CHECK(helix::ui::clamp_screensaver_type(99) == last);
}

TEST_CASE("every registered screensaver name resolves to its row",
          "[screensaver][screensaver_registry]") {
    for (const helix::ui::ScreensaverInfo& info : SCREENSAVERS) {
        CAPTURE(info.name);
        const helix::ui::ScreensaverInfo* by_name = helix::ui::find_screensaver_by_name(info.name);
        REQUIRE(by_name != nullptr);
        CHECK(by_name->type == info.type);
        CHECK(helix::ui::find_screensaver(info.type) == by_name);
        CHECK(helix::ui::resolve_screensaver_now(info.name, ScreensaverType::OFF) == info.type);
    }
    CHECK(helix::ui::find_screensaver_by_name("") == nullptr);
    CHECK(helix::ui::find_screensaver_by_name("Starfield") == nullptr);
    CHECK(helix::ui::find_screensaver(ScreensaverType::OFF) == nullptr);
}

TEST_CASE("HELIX_SCREENSAVER_NOW falls back to the configured saver, then flying toasters",
          "[screensaver][screensaver_registry]") {
    using helix::ui::resolve_screensaver_now;
    CHECK(resolve_screensaver_now("1", ScreensaverType::STARFIELD) == ScreensaverType::STARFIELD);
    CHECK(resolve_screensaver_now("bogus", ScreensaverType::PIPES_3D) == ScreensaverType::PIPES_3D);
    CHECK(resolve_screensaver_now("1", ScreensaverType::OFF) == ScreensaverType::FLYING_TOASTERS);
}

TEST_CASE("the settings dropdown lists Off then every registered screensaver in type order",
          "[screensaver][screensaver_registry]") {
    std::ifstream file("ui_xml/settings_display_overlay.xml");
    REQUIRE(file.is_open()); // helix-tests runs from the repo root
    std::stringstream buffer;
    buffer << file.rdbuf();
    const std::string xml = buffer.str();

    std::vector<std::string> expected{helix::ui::SCREENSAVER_OFF_LABEL_KEY};
    for (const helix::ui::ScreensaverInfo& info : SCREENSAVERS) {
        expected.emplace_back(info.label_key);
    }
    CHECK(split_option_lines(attribute_of_named(xml, "row_screensaver", "options")) == expected);
    CHECK(split_option_lines(attribute_of_named(xml, "row_screensaver", "options_tag")) ==
          expected);
}

TEST_CASE("every screensaver label the dropdown can show has an en.xml translation",
          "[screensaver][screensaver_registry]") {
    std::ifstream file("ui_xml/translations/en.xml");
    REQUIRE(file.is_open()); // helix-tests runs from the repo root
    std::stringstream buffer;
    buffer << file.rdbuf();
    const std::string xml = buffer.str();

    // en.xml is the master list the other locales are generated from, so a saver
    // missing here ships untranslated everywhere.
    std::vector<std::string> tags{helix::ui::SCREENSAVER_OFF_LABEL_KEY};
    for (const helix::ui::ScreensaverInfo& info : SCREENSAVERS) {
        tags.emplace_back(info.label_key);
    }
    for (const std::string& tag : tags) {
        CAPTURE(tag);
        CHECK(xml.find("<translation tag=\"" + tag + "\"") != std::string::npos);
    }
}

TEST_CASE("fresh installs default to flying toasters, a registered saver",
          "[screensaver][screensaver_registry]") {
    CHECK(helix::ui::DEFAULT_SCREENSAVER_TYPE == ScreensaverType::FLYING_TOASTERS);
    CHECK(helix::ui::find_screensaver(helix::ui::DEFAULT_SCREENSAVER_TYPE) != nullptr);
}

TEST_CASE("the fresh-install default is a saver this build can actually draw",
          "[screensaver][screensaver_registry]") {
    using helix::ui::default_screensaver_type;
    using helix::ui::find_screensaver;
    using helix::ui::SAVER_DEPTH_16;
    using helix::ui::SAVER_DEPTH_32;

    SECTION("a 32 bpp build gets the preferred default") {
        CHECK(default_screensaver_type(SAVER_DEPTH_32) == helix::ui::DEFAULT_SCREENSAVER_TYPE);
    }

    SECTION("a 16 bpp build does not get a 32 bpp-only saver") {
        const ScreensaverType chosen = default_screensaver_type(SAVER_DEPTH_16);
        REQUIRE(chosen != ScreensaverType::OFF);
        const helix::ui::ScreensaverInfo* row = find_screensaver(chosen);
        REQUIRE(row != nullptr);
        CAPTURE(row->name);
        CHECK((row->depths & SAVER_DEPTH_16) != 0U);
    }

    SECTION("whatever is chosen draws at the depth it was chosen for") {
        for (const uint8_t depth : {SAVER_DEPTH_16, SAVER_DEPTH_32}) {
            CAPTURE(depth);
            const ScreensaverType chosen = default_screensaver_type(depth);
            const helix::ui::ScreensaverInfo* row = find_screensaver(chosen);
            REQUIRE(row != nullptr);
            CAPTURE(row->name);
            CHECK((row->depths & depth) != 0U);
        }
    }
}
