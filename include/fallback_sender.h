// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Which HelixScreen on a printer acts for a plugin feature's app fallback,
// decided against the live connection. The rule itself is
// fallback_sender_role() (helix_plugin_features.h); this reads its inputs.

#include "async_lifetime_guard.h"
#include "helix_plugin_features.h"

#include <functional>

namespace helix {

/// This instance's fallback_sender_role(): at once on the printer's host, else
/// after reading the local screen's presence record (kLocalScreenPresenceKey).
/// @p then runs on the main thread, and not at all once @p token has expired.
void resolve_fallback_sender_role(LifetimeToken token,
                                  std::function<void(FallbackSenderRole)> then);

} // namespace helix
