// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "helix/ui/text_metrics.h"

#include "lvgl/src/misc/lv_text_private.h" // lv_text_get_width

#include <cstring>

namespace helix::ui {

int32_t text_width_n(const char* txt, uint32_t len, const lv_font_t* font, int32_t letter_space) {
    if (!txt || !font || len == 0) {
        return 0;
    }
    lv_text_attributes_t attributes = {};
    attributes.letter_space = letter_space;
    attributes.max_width = LV_COORD_MAX;
    attributes.text_flags = LV_TEXT_FLAG_NONE;
    return lv_text_get_width(txt, len, font, &attributes);
}

int32_t text_width(const char* txt, const lv_font_t* font, int32_t letter_space) {
    if (!txt) {
        return 0;
    }
    return text_width_n(txt, static_cast<uint32_t>(std::strlen(txt)), font, letter_space);
}

} // namespace helix::ui
