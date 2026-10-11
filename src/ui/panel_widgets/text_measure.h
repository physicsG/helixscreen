// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "lvgl/lvgl.h"
#include "lvgl/src/misc/lv_text_private.h" // lv_text_get_width, lv_text_attributes_t

namespace helix::ui {

/// Pixel width of a UTF-8 string in `font`. lv_text_get_width dereferences its
/// attributes argument, so it gets a zeroed block (no recolor, zero letter
/// space, unbounded width); NULL crashes.
inline int measure_text_px(const char* txt, const lv_font_t* font) {
    if (!txt || !font)
        return 0;
    lv_text_attributes_t attrs;
    lv_text_attributes_init(&attrs);
    attrs.letter_space = 0;
    attrs.max_width = LV_COORD_MAX;
    return lv_text_get_width(txt, LV_TEXT_LEN_MAX, font, &attrs);
}

} // namespace helix::ui
