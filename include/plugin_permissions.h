// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <optional>
#include <set>
#include <string_view>
#include <vector>

namespace helix::plugin {

/// A capability a plugin declares in its manifest before the matching binding works.
enum class Permission { Gcode, MoonrakerWrite, Http, Storage };

using PermissionSet = std::set<Permission>;

std::optional<Permission> permission_from_string(std::string_view name);
const char* permission_name(Permission p);

/// Permissions in `requested` that `granted` does not cover, in enum order. Non-empty means
/// the user consents again before the plugin loads.
std::vector<Permission> permission_growth(const PermissionSet& granted,
                                          const PermissionSet& requested);

/// Moonraker methods `helix.moonraker.call` accepts without `moonraker_write`.
bool is_readonly_moonraker_method(std::string_view method);

} // namespace helix::plugin
