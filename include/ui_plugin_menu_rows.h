// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui_context_menu.h"

#include "plugin_menu.h"

#include <lvgl.h>
#include <vector>

namespace helix::ui {

/// How the rows are drawn.
enum class PluginRowLayout {
    List,   ///< a list of their own: plugin_menu_heading + plugin_menu_row
    Column, ///< a column group beside a card's own: its heading and button rows
};

/// Appends a divider (when `divider`), then per plugin a heading row with the plugin's
/// name and its item rows. A disabled entry is greyed, with its reason as the row's
/// second line. A tap hides `menu` and activates the entry with `context`. Each row owns
/// its tap payload. Returns the item rows added; nothing at all is added for no entries.
int append_plugin_rows(ContextMenu& menu, lv_obj_t* container,
                       std::vector<plugin_menu::Entry> entries, plugin_menu::json context,
                       bool divider = true, PluginRowLayout layout = PluginRowLayout::List);

/// Registers the XML callback the plugin rows use. Idempotent.
void register_plugin_menu_row_callbacks();

} // namespace helix::ui
