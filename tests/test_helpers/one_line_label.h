// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "lvgl/lvgl.h"

namespace helix::test {

/// The height helix::ui::label_hold_one_line() gives @p label, derived here
/// from the label's own font and spacing.
inline int32_t one_line_height(lv_obj_t* label) {
    return lv_font_get_line_height(lv_obj_get_style_text_font(label, LV_PART_MAIN)) +
           lv_obj_get_style_space_top(label, LV_PART_MAIN) +
           lv_obj_get_style_space_bottom(label, LV_PART_MAIN);
}

/// Whether @p label carries a pinned one-line height. Reads the style, not the
/// laid-out size, so a short string at content height does not pass.
inline bool held_to_one_line(lv_obj_t* label) {
    return label && lv_obj_get_style_height(label, LV_PART_MAIN) == one_line_height(label);
}

/// First child of @p parent that is a label in DOTS mode, or nullptr.
inline lv_obj_t* find_dotted_label(lv_obj_t* parent) {
    if (!parent)
        return nullptr;
    for (uint32_t i = 0; i < lv_obj_get_child_count(parent); ++i) {
        lv_obj_t* child = lv_obj_get_child(parent, static_cast<int32_t>(i));
        if (lv_obj_check_type(child, &lv_label_class) &&
            lv_label_get_long_mode(child) == LV_LABEL_LONG_MODE_DOTS)
            return child;
        if (lv_obj_t* nested = find_dotted_label(child))
            return nested;
    }
    return nullptr;
}

} // namespace helix::test
