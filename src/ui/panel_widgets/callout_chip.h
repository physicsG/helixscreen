// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The pinned (compact) callout chip, shared by the home printer image widget
// and the image tagger's review so both draw and size it the same way. A
// compact chip is styles.activity_chip around its text in font_xs, with no
// icon; the light chip, which has no text, keeps its xs icon.

#include "text_measure.h"
#include "theme_manager.h"

#include <algorithm>
#include <string>

namespace helix::ui {

/// A pinned chip sits on the picture, so it drops the unit letter ("60 / 70°").
inline std::string compact_callout_text(std::string s) {
    static constexpr char kDegree[] = "°";
    for (size_t p; (p = s.find("°C")) != std::string::npos;)
        s.erase(p + sizeof(kDegree) - 1, 1);
    return s;
}

/// Width of a compact chip styled like `chip`: chrome plus its compact text in
/// font_xs and a comfort margin (measured and rendered text disagree by a few
/// pixels). An icon-only chip (empty text) is chrome plus `icon_px`.
inline int compact_callout_chip_w(lv_obj_t* chip, const std::string& text, int icon_px) {
    const int chrome = lv_obj_get_style_pad_left(chip, LV_PART_MAIN) +
                       lv_obj_get_style_pad_right(chip, LV_PART_MAIN) +
                       2 * lv_obj_get_style_border_width(chip, LV_PART_MAIN);
    if (text.empty())
        return chrome + icon_px;
    return chrome +
           measure_text_px(compact_callout_text(text).c_str(), theme_manager_get_font("font_xs")) +
           theme_manager_get_spacing("space_md");
}

/// Height of a compact chip styled like `chip`. At some breakpoints the xs
/// icon line is taller than a font_xs line, so a layout with the light chip
/// showing slots every chip at the icon's height.
inline int compact_callout_chip_h(lv_obj_t* chip, bool with_icon) {
    int line = lv_font_get_line_height(theme_manager_get_font("font_xs"));
    if (with_icon)
        line = std::max(line, int(lv_font_get_line_height(theme_manager_get_font("icon_font_xs"))));
    return line + lv_obj_get_style_pad_top(chip, LV_PART_MAIN) +
           lv_obj_get_style_pad_bottom(chip, LV_PART_MAIN) +
           2 * lv_obj_get_style_border_width(chip, LV_PART_MAIN);
}

} // namespace helix::ui
