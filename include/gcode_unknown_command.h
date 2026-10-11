// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cctype>
#include <optional>
#include <string>
#include <string_view>

namespace helix {

/// Pure: extract the command name from Klipper's `Unknown command:"X"` response,
/// nullopt for anything else. The `//` console prefix may be present or already
/// stripped.
///
/// This one shape is unambiguous and, unlike a `!!` error, is how Klipper reports
/// a command it does not define: a console line, after which the script carries on
/// and Moonraker still answers `ok`. Nothing else in the stack can tell "the macro
/// ran" from "one of its commands never existed". Deliberately narrow: it is not a
/// general terminating-response classifier.
[[nodiscard]] inline std::optional<std::string> parse_unknown_command(const std::string& line) {
    size_t i = line.find_first_not_of(" \t");
    if (i == std::string::npos) {
        return std::nullopt;
    }
    if (line.compare(i, 2, "//") == 0) {
        i = line.find_first_not_of(" \t", i + 2);
        if (i == std::string::npos) {
            return std::nullopt;
        }
    }

    // Anchored at the start of the body so an ordinary console line that happens
    // to mention the phrase cannot claim to be one.
    static constexpr std::string_view PREFIX = "unknown command";
    if (line.size() - i <= PREFIX.size()) {
        return std::nullopt;
    }
    for (size_t k = 0; k < PREFIX.size(); ++k) {
        if (static_cast<char>(std::tolower(static_cast<unsigned char>(line[i + k]))) != PREFIX[k]) {
            return std::nullopt;
        }
    }

    // Only a colon and whitespace may separate the phrase from the quoted name.
    i += PREFIX.size();
    while (i < line.size() && (line[i] == ':' || line[i] == ' ' || line[i] == '\t')) {
        ++i;
    }
    if (i >= line.size() || line[i] != '"') {
        return std::nullopt;
    }

    size_t close = line.find('"', i + 1);
    if (close == std::string::npos || close == i + 1) {
        return std::nullopt;
    }
    return line.substr(i + 1, close - i - 1);
}

/// Pure: true when a name parse_unknown_command() returned is leftover Jinja syntax
/// (`%}`, `{`, `}}`) rather than a command anyone could define. A malformed tag in
/// a macro renders as its own G-code line; Klipper reports it as unknown and runs
/// the rest of the macro, so the macro did not stop and no command is missing.
[[nodiscard]] inline bool is_template_residue(std::string_view name) {
    return name.find_first_of("{}%") != std::string_view::npos;
}

} // namespace helix
