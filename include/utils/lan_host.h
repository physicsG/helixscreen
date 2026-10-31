// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string_view>

struct sockaddr; // NAMESPACE_OK: forward declaration of the POSIX socket type

namespace helix::net {

/// True when @p host names a machine on the local network segment: an RFC1918,
/// link-local or IPv6 unique-local literal (brackets and a `%zone` suffix are
/// accepted), or an mDNS `.local` name. CGNAT 100.64/10 is not LAN, because
/// Tailscale assigns its addresses from it; loopback, multicast and any other
/// host name are not either, so they keep the system's default route.
bool is_lan_host(std::string_view host);

/// is_lan_host() for a resolved peer address. False for anything that is not
/// AF_INET or AF_INET6, and for nullptr.
bool is_lan_address(const struct sockaddr* addr);

} // namespace helix::net
