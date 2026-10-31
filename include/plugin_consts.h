// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "lvgl/lvgl.h"
#include "theme_manager.h"

#include <string>
#include <vector>

namespace helix::plugin {

/// Every <consts> entry across one plugin's XML files.
struct PluginConsts {
    struct Plain {
        std::string type; ///< px, int, percentage, color, string
        std::string name;
        std::string value;
        bool operator==(const Plain& o) const {
            return type == o.type && name == o.name && value == o.value;
        }
    };
    /// px constants with a breakpoint suffix, by tier, keyed by base name.
    PxTierTables px;
    /// Everything else, registered as written (px values DPI-scaled).
    std::vector<Plain> plain;
};

/// Reads the <consts> blocks of @p xml_files (file contents, already policy-checked).
PluginConsts collect_plugin_consts(const std::vector<std::string>& xml_files);

/// One message per base name that has some tier variants but not the full
/// _small/_medium/_large set, naming the missing tiers.
std::vector<std::string> missing_tier_errors(const PxTierTables& px);

/// Registers (or updates) the plugin's constants in the shared constant scope,
/// so every component of the plugin can use them: each triplet resolved for
/// @p display, plain constants as written.
void register_plugin_consts(const PluginConsts& c, lv_display_t* display);

} // namespace helix::plugin
