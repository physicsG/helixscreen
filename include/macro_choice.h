// Copyright (C) 2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>

namespace helix {

/// The stored choice meaning "whatever discovery found" in a macro picker.
inline constexpr const char* kAutoMacro = "auto";

/// The macro a picker's stored choice sends: @p detected for kAutoMacro or an
/// empty choice, otherwise the choice verbatim. An explicit name is honored even
/// when discovery has not seen it; Klipper's own error is the honest signal.
[[nodiscard]] inline std::string resolve_macro_choice(const std::string& choice,
                                                      const std::string& detected) {
    return (choice.empty() || choice == kAutoMacro) ? detected : choice;
}

/// The stored choice as the picker shows it: kAutoMacro for an empty one.
[[nodiscard]] inline std::string macro_choice_or_auto(const std::string& choice) {
    return choice.empty() ? std::string(kAutoMacro) : choice;
}

} // namespace helix
