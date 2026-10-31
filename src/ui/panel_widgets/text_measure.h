// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "helix/ui/text_metrics.h"
#include "lvgl/lvgl.h"

#include <algorithm>
#include <string>

namespace helix::ui {

/// Pixel width of a UTF-8 string in `font`; nullptr text or font measures 0.
inline int measure_text_px(const char* txt, const lv_font_t* font) {
    return text_width(txt, font);
}

inline int measure_text_px(const std::string& txt, const lv_font_t* font) {
    return measure_text_px(txt.c_str(), font);
}

/// The widest piece of @p text between `sep` characters, in `font`.
inline int widest_piece_px(const std::string& text, const lv_font_t* font, char sep) {
    int widest = 0;
    size_t start = 0;
    while (start <= text.size()) {
        const size_t end = std::min(text.find(sep, start), text.size());
        if (end > start) {
            widest = std::max(widest, measure_text_px(text.substr(start, end - start), font));
        }
        start = end + 1;
    }
    return widest;
}

/// The widest single word of @p text in `font`: the least width a label
/// wrapping at spaces needs.
inline int widest_word_px(const std::string& text, const lv_font_t* font) {
    return widest_piece_px(text, font, ' ');
}

/// The widest line of @p text in `font`: the width a label showing every line needs.
inline int widest_line_px(const std::string& text, const lv_font_t* font) {
    return widest_piece_px(text, font, '\n');
}

/// How many lines @p text breaks into at its newlines.
inline int text_line_count(const std::string& text) {
    return 1 + int(std::count(text.begin(), text.end(), '\n'));
}

} // namespace helix::ui
