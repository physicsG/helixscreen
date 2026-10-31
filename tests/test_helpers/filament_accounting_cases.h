// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// tests/fixtures/filament_accounting/cases.json: the filament accounting rules
// the C++ and plugin suites both read.

#include <fstream>
#include <iterator>
#include <string>

#include "hv/json.hpp"

namespace helix::test {

/// The parsed case table; discarded JSON when the file cannot be read.
inline nlohmann::json filament_accounting_cases() {
    std::string src = __FILE__;
    const auto pos = src.rfind("/tests/test_helpers/");
    const std::string root = pos != std::string::npos ? src.substr(0, pos) : ".";
    std::ifstream f(root + "/tests/fixtures/filament_accounting/cases.json");
    std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return nlohmann::json::parse(text, nullptr, false);
}

/// The ownership rows naming @p backend.
inline nlohmann::json ownership_rows_for(const std::string& backend) {
    nlohmann::json rows = nlohmann::json::array();
    const nlohmann::json table = filament_accounting_cases();
    if (table.is_discarded()) {
        return rows;
    }
    for (const nlohmann::json& row : table["ownership"]) {
        if (row["backend"].is_string() && row["backend"].get<std::string>() == backend) {
            rows.push_back(row);
        }
    }
    return rows;
}

} // namespace helix::test
