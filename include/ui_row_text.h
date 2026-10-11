// Copyright (C) 2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "lvgl/lvgl.h"

namespace helix::ui {

/// Sets the text of the label named @p label_name inside an XML-created row.
///
/// For text a user, the printer or a plugin supplies (printer, file, object,
/// material, device, macro and widget names). helix-xml resolves a prop value
/// starting with '#' as a const reference and drops it when no such const
/// exists, so that text cannot travel as a prop: the label would come up blank.
inline void set_row_label_text(lv_obj_t* row, const char* label_name, const char* text) {
    lv_obj_t* label = row ? lv_obj_find_by_name(row, label_name) : nullptr;
    if (label) {
        // DECLARATIVE_OK: user text; as a prop a leading '#' reads as a const
        lv_label_set_text(label, text ? text : "");
    }
}

} // namespace helix::ui
