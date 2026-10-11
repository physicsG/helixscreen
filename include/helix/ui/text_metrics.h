// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "lvgl/lvgl.h"

#include <cstdint>

namespace helix::ui {

/**
 * @brief Drawn width of @p txt in @p font, as lv_draw_label would lay it out
 *
 * Measuring by any other route (byte counts, glyph advances summed by hand)
 * drifts from what is rendered, and a layout decided on a width the label does
 * not draw clips or leaves a gap.
 *
 * A measured-exact fit still renders as an overlap by a pixel or two, so a
 * caller budgeting text should keep a comfort margin rather than treating this
 * as an upper bound.
 *
 * @param txt           NUL-terminated text; nullptr measures 0
 * @param font          face to measure in; nullptr measures 0
 * @param letter_space  extra tracking the label will apply
 */
int32_t text_width(const char* txt, const lv_font_t* font, int32_t letter_space = 0);

} // namespace helix::ui
