// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace helix::text_io {

/// One decoded UTF-8 sequence: its codepoint and how many bytes it spans.
struct Utf8Sequence {
    uint32_t codepoint;
    size_t length;
};

/// The well-formed UTF-8 sequence starting at byte @p i of @p s, or nullopt
/// for a stray continuation byte or a truncated, overlong or surrogate
/// sequence, or one past U+10FFFF.
inline std::optional<Utf8Sequence> decode_utf8(std::string_view s, size_t i) {
    if (i >= s.size()) {
        return std::nullopt;
    }
    const auto lead = static_cast<unsigned char>(s[i]);
    if (lead < 0x80) {
        return Utf8Sequence{lead, 1};
    }
    size_t length = 0;
    uint32_t cp = 0;
    if ((lead & 0xE0) == 0xC0) {
        length = 2;
        cp = lead & 0x1F;
    } else if ((lead & 0xF0) == 0xE0) {
        length = 3;
        cp = lead & 0x0F;
    } else if ((lead & 0xF8) == 0xF0) {
        length = 4;
        cp = lead & 0x07;
    } else {
        return std::nullopt;
    }
    if (i + length > s.size()) {
        return std::nullopt;
    }
    for (size_t k = 1; k < length; ++k) {
        const auto cont = static_cast<unsigned char>(s[i + k]);
        if ((cont & 0xC0) != 0x80) {
            return std::nullopt;
        }
        cp = (cp << 6) | (cont & 0x3F);
    }
    static constexpr uint32_t kMinForLength[] = {0, 0, 0x80, 0x800, 0x10000};
    if (cp < kMinForLength[length] || (cp >= 0xD800 && cp <= 0xDFFF) || cp > 0x10FFFF) {
        return std::nullopt;
    }
    return Utf8Sequence{cp, length};
}

/// Whether `s` is well-formed UTF-8 throughout (decode_utf8() at every sequence).
inline bool is_valid_utf8(std::string_view s) {
    size_t i = 0;
    while (i < s.size()) {
        const std::optional<Utf8Sequence> seq = decode_utf8(s, i);
        if (!seq) {
            return false;
        }
        i += seq->length;
    }
    return true;
}

} // namespace helix::text_io
