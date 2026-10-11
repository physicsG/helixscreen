// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#if HELIX_HAS_PLUGINS

#include "plugin_manifest.h"

#include <functional>
#include <string>
#include <vector>

namespace helix::plugin {

/// One plain-words line per permission, in enum order.
std::vector<std::string> consent_lines(const PermissionSet& perms);

/// The consent dialog body for enabling `m`, or for approving `grown` on an
/// update when `grown` is non-empty.
std::string consent_message(const Manifest& m, const std::vector<Permission>& grown);

/// Shows the consent dialog; `on_yes` runs only on confirm.
void show_consent(const Manifest& m, const std::vector<Permission>& grown,
                  std::function<void()> on_yes);

} // namespace helix::plugin

#endif // HELIX_HAS_PLUGINS
