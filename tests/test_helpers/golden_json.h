// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdlib>
#include <fstream>
#include <string>

#include "../catch_amalgamated.hpp"
#include "hv/json.hpp"

namespace helix::test {

/// Absolute path of tests/fixtures/<dir>/<name>.json, derived from this header's own location.
inline std::string golden_path(const std::string& dir, const std::string& name) {
    std::string src = __FILE__;
    auto pos = src.rfind("/tests/");
    std::string root = pos != std::string::npos ? src.substr(0, pos) + "/" : std::string{};
    return root + "tests/fixtures/" + dir + "/" + name + ".json";
}

/// Compares `actual` with the golden file tests/fixtures/<dir>/<name>.json. With
/// HELIX_WRITE_GOLDENS=1 the file is rewritten from `actual` instead.
inline void check_against_golden(const std::string& dir, const std::string& name,
                                 const nlohmann::json& actual) {
    const std::string path = golden_path(dir, name);
    const char* write = std::getenv("HELIX_WRITE_GOLDENS");
    if (write != nullptr && std::string(write) == "1") {
        std::ofstream out(path);
        REQUIRE(out.good());
        out << actual.dump(2) << "\n";
        WARN("wrote golden " << path);
        return;
    }
    std::ifstream in(path);
    INFO("golden missing or unreadable: " << path);
    REQUIRE(in.good());
    nlohmann::json expected = nlohmann::json::parse(in, nullptr, false);
    REQUIRE_FALSE(expected.is_discarded());
    INFO("actual:\n" << actual.dump(2));
    INFO("expected:\n" << expected.dump(2));
    CHECK(actual == expected);
}

} // namespace helix::test
