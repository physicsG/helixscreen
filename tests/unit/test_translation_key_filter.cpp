// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

// helix::ui::is_translation_key() decides which literals the XML engine gives an
// implied translation tag; the extractor's should_skip_text() decides which ones
// become catalog keys. tests/fixtures/translation_key_cases.json holds the
// verdicts both must reach, and tests/python/test_implied_tags.py checks the
// Python side against the same file.

#include "translation_loader.h"

#include <fstream>
#include <string>

#include "../catch_amalgamated.hpp"
#include "hv/json.hpp"

namespace {

std::string fixture_path() {
    std::string src = __FILE__;
    auto pos = src.rfind("/tests/unit/");
    std::string root = pos != std::string::npos ? src.substr(0, pos) + "/" : "";
    return root + "tests/fixtures/translation_key_cases.json";
}

} // namespace

TEST_CASE("is_translation_key agrees with the extractor on every fixture case",
          "[translation][implied_tags]") {
    std::ifstream f(fixture_path());
    REQUIRE(f.is_open());
    auto j = nlohmann::json::parse(f, nullptr, false);
    REQUIRE_FALSE(j.is_discarded());
    const auto& cases = j["cases"];
    REQUIRE(cases.size() > 1000);

    size_t disagreements = 0;
    for (const auto& c : cases) {
        const std::string text = c[0].get<std::string>();
        const bool expected = c[1].get<bool>();
        if (helix::ui::is_translation_key(text) != expected) {
            ++disagreements;
            UNSCOPED_INFO("\"" << text << "\" expected " << (expected ? "key" : "non-key"));
        }
    }
    CHECK(disagreements == 0);
}
