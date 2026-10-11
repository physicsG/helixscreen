// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file decimal_parse.h
 * @brief Header-only stand-in for std::from_chars<float>.
 *
 * GCC 10 (AD5M toolchain) has no std::from_chars overload for floating
 * point, and Apple libc++ deletes it, so nothing in this tree may call
 * std::from_chars<float> directly. parse_decimal() matches its grammar and
 * {ptr, ec} contract, reading ASCII digits directly instead of going
 * through strtof/atof. That keeps it independent of LC_NUMERIC by
 * construction rather than by the convention that nothing setlocale()s it.
 */

#pragma once

#include <limits>
#include <system_error>

namespace helix {

/// Result of parse_decimal(), matching std::from_chars's {ptr, ec} contract:
/// ec == std::errc{} on success, with ptr past the consumed characters. No
/// recognizable number: ptr == first, ec == std::errc::invalid_argument.
/// A recognizable number too large for float: ptr past the consumed
/// characters (as on success) but value is left unmodified and
/// ec == std::errc::result_out_of_range.
struct DecimalParseResult {
    const char* ptr;
    std::errc ec;
};

/// Locale-independent float parse over [first, last), matching
/// std::from_chars<float>'s grammar and error reporting: an optional '-' (a
/// leading '+' is not part of the grammar and is rejected, same as
/// std::from_chars), a digit sequence, an optional '.' with a digit
/// sequence, and no exponent -- none of this tree's callers need scientific
/// notation. A magnitude too large for float is rejected with
/// result_out_of_range, leaving value unmodified, the same as
/// std::from_chars.
inline DecimalParseResult parse_decimal(const char* first, const char* last, float& value) {
    const char* p = first;
    bool negative = false;
    if (p != last && *p == '-') {
        negative = true;
        ++p;
    }

    const char* int_start = p;
    double whole = 0.0;
    while (p != last && *p >= '0' && *p <= '9') {
        whole = whole * 10.0 + static_cast<double>(*p - '0');
        ++p;
    }
    const bool have_int_digits = (p != int_start);

    double frac = 0.0;
    bool have_frac_digits = false;
    if (p != last && *p == '.') {
        ++p;
        const char* frac_start = p;
        double scale = 1.0;
        while (p != last && *p >= '0' && *p <= '9') {
            scale *= 10.0;
            frac += static_cast<double>(*p - '0') / scale;
            ++p;
        }
        have_frac_digits = (p != frac_start);
    }

    if (!have_int_digits && !have_frac_digits) {
        return {first, std::errc::invalid_argument};
    }
    const double magnitude = whole + frac;
    if (magnitude > static_cast<double>(std::numeric_limits<float>::max())) {
        return {p, std::errc::result_out_of_range};
    }
    value = static_cast<float>(negative ? -magnitude : magnitude);
    return {p, std::errc{}};
}

} // namespace helix
