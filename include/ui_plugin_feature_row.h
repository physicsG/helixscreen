// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The callbacks behind plugin_feature_toggle_row.xml, the settings row for a
// switch backed by a HelixPrint plugin feature. The row's look comes from
// PrinterPluginStatusState's per-feature mode subjects; this file handles
// what its controls do.

#include <lvgl.h>
#include <string>

namespace helix::ui {

/// Register the row's callbacks and its constant `plugin_feature_greyed`
/// subject. Before any XML that uses the row is created.
void register_plugin_feature_row_callbacks();

/// The feature of the plugin_feature_toggle_row a control sits in, from its
/// switch row's `plugin_feature_row_<feature>` name; empty outside one.
std::string plugin_feature_of(lv_obj_t* obj);

/// Write the user's opt-in for plugin feature @p name to
/// helix-screen/plugin_features, then re-read the plugin's feature map so every
/// surface shows the plugin's verdict.
void write_plugin_feature_opt_in(const std::string& name, bool enabled);

} // namespace helix::ui
