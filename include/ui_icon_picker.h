// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "lvgl/lvgl.h"

#include <functional>
#include <string_view>

namespace helix::ui {

/// Fill @p grid with one tappable glyph cell per entry of @p icons, the cell
/// naming @p selected outlined. A tap calls @p on_pick with the icon name; the
/// caller owns what happens next, including calling refresh_icon_grid(). The
/// callback must not outlive the grid's owner: it dies with the grid.
void populate_icon_grid(lv_obj_t* grid, const char* const* icons, size_t count,
                        std::string_view selected, std::function<void(const char*)> on_pick);

/// Move the selection outline to the cell naming @p selected.
void refresh_icon_grid(lv_obj_t* grid, std::string_view selected);

} // namespace helix::ui
