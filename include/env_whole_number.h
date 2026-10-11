// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <spdlog/spdlog.h>

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <optional>

namespace helix {

/**
 * @brief A whole decimal number in [min_value, max_value] from the environment
 *
 * Unset reads as nullopt. A set value that is not a plain decimal number in range reads as
 * nullopt with the warning "<log_tag> Ignoring NAME='value': expected <what>, min to max".
 * strtoul skips leading whitespace and accepts a sign, so only a value whose first character
 * is a digit is parsed.
 */
inline std::optional<uint32_t> whole_number_from_env(const char* name, uint32_t min_value,
                                                     uint32_t max_value, const char* log_tag,
                                                     const char* what) {
    const char* value = std::getenv(name);
    if (value == nullptr) {
        return std::nullopt;
    }
    bool ok = *value >= '0' && *value <= '9';
    unsigned long parsed = 0;
    if (ok) {
        errno = 0;
        char* end = nullptr;
        parsed = std::strtoul(value, &end, 10);
        ok = errno == 0 && *end == '\0' && parsed >= min_value && parsed <= max_value;
    }
    if (!ok) {
        spdlog::warn("{} Ignoring {}='{}': expected {}, {} to {}", log_tag, name, value, what,
                     min_value, max_value);
        return std::nullopt;
    }
    return static_cast<uint32_t>(parsed);
}

} // namespace helix
