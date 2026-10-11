// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plugin_permissions.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "hv/json.hpp"

namespace helix::plugin {

using json = nlohmann::json;

enum class SettingType { Bool, Int, Float, Enum, String, Action, Info };

constexpr int kMaxWidgetCells = 8;
constexpr size_t kMaxWidgetsPerPlugin = 8;

/// One entry of the manifest's `settings` array.
struct SettingDecl {
    std::string key;
    std::string label;
    SettingType type = SettingType::Bool;
    json default_value;               ///< null when the manifest gives none
    double min = 0;                   ///< Int and Float
    double max = 0;                   ///< Int and Float
    std::vector<std::string> options; ///< Enum
    std::string callback;             ///< Action: handler name, owned by the plugin
    std::string subject;              ///< Info: subject name, owned by the plugin
};

/// One entry of the manifest's `widgets` array. Spans are in grid tracks, half
/// a cell each; the manifest writes them in cells.
struct WidgetDecl {
    std::string id; ///< <plugin>__<name>
    std::string name;
    std::string icon; ///< icon name; empty when absent
    std::string description;
    std::string component; ///< <plugin>__<name>, a file in ui/
    int colspan = 2;
    int rowspan = 2;
    int max_colspan = 0; ///< 0: not resizable on this axis
    int max_rowspan = 0;
    bool half_cells = false; ///< resizes and may be authored in half cells
};

struct Manifest {
    std::string id;
    std::string name;
    std::string version;
    std::string author;
    std::string description;
    std::string helix_version; ///< version constraint, empty when absent
    PermissionSet permissions;
    int memory_mb = 2;
    std::vector<SettingDecl> settings;
    std::string settings_overlay; ///< XML component name, empty when absent
    std::vector<WidgetDecl> widgets;
};

/// `manifest` is set only when `errors` is empty.
struct ManifestParse {
    std::optional<Manifest> manifest;
    std::vector<std::string> errors;
};

ManifestParse parse_manifest(const std::string& text);

/// `^[a-z][a-z0-9-]{1,31}$`. No underscore, so the id cannot contain the
/// ownership separator.
bool is_valid_plugin_id(std::string_view id);

/// Separator between a plugin id and the rest of a name that plugin owns.
/// Doubled because app names share the single-underscore space (`ams_*`,
/// `extruder_target`, `settings_*`) and no app name contains `__`: only the
/// double underscore proves the name is the plugin's.
constexpr std::string_view kPluginNameSeparator = "__";

/// The full form of a name a plugin owns: `<id>__<rest>`.
std::string plugin_owned_name(std::string_view id, std::string_view rest);

/// True for `<id>__<rest>` with a non-empty rest.
bool is_owned_name(std::string_view id, std::string_view name);

/// The owning plugin id of `<id>__<rest>`; empty when the name is not in that
/// form.
std::string_view owner_of(std::string_view name);

} // namespace helix::plugin
