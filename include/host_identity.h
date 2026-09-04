// Copyright (C) 2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>
#include <string_view>

namespace helix {

/// True when the given Moonraker host string refers to the machine running
/// helixscreen. Checked in order: loopback literals → gethostname() →
/// getifaddrs() scan. Result cached per distinct host string.
bool is_moonraker_on_same_host(std::string_view host);

/// Extract the host portion of a websocket URL ("ws://host:port/..." or
/// "wss://..."; bracketed IPv6 handled). Returns "" for empty input or an
/// unknown scheme. Canonical home for "which host are we talking to" —
/// keep URL parsing and host-identity checks together.
[[nodiscard]] std::string extract_host_from_websocket_url(const std::string& url);

/// Drop the detection cache. Call when moonraker_host changes at runtime.
void invalidate_host_identity_cache();

} // namespace helix
