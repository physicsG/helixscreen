// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "helix/ui/shared_font_style.h"

#include <deque>

namespace helix::ui {

namespace {

/// One entry per face any caller has ever asked for, which is at most every
/// face the build links. A deque, so each style keeps its address as entries
/// are added: objects hold pointers to them.
struct FaceStyle {
    const lv_font_t* font;
    lv_style_t style;
};
/// Never destroyed: live objects keep pointing at these styles until the process
/// ends, so an exit-time destructor would orphan each style's property array.
std::deque<FaceStyle>& face_styles() {
    static auto* styles = new std::deque<FaceStyle>();
    return *styles;
}

} // namespace

lv_style_t* shared_font_style(const lv_font_t* font) {
    auto& styles = face_styles();
    for (auto& entry : styles) {
        if (entry.font == font)
            return &entry.style;
    }
    FaceStyle& entry = styles.emplace_back();
    entry.font = font;
    lv_style_init(&entry.style);
    lv_style_set_text_font(&entry.style, font);
    return &entry.style;
}

void apply_font_style(lv_obj_t* obj, const lv_font_t* font) {
    if (!obj || !font)
        return;

    // Drop whichever face was applied before, then add the one wanted. A
    // widget that resolves its face more than once must REPLACE the style
    // rather than stack a second one, and the face it already carries cannot
    // be recomputed from its size rung: a breakpoint change re-points the
    // icon_font_* tokens, so the rung names a different face than the one the
    // object is carrying.
    // Removing a style the object does not carry is a no-op.
    for (auto& entry : face_styles()) {
        lv_obj_remove_style(obj, &entry.style, LV_PART_MAIN);
    }
    lv_obj_add_style(obj, shared_font_style(font), LV_PART_MAIN);
}

} // namespace helix::ui
