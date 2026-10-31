// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

namespace helix {

/// Bind and listen on @p host:@p port (IPv4), reporting the bound port.
///
/// libhv's http_server_run() skips binding entirely when `port == 0` (it guards
/// on `port > 0`), so asking it for an ephemeral port yields a server that never
/// listens. It does honour a pre-bound descriptor via setListenFD(), and handing
/// it one is also race-free: picking a port, closing, and re-binding would leave a
/// window for another process to take it.
///
/// @param port 0 for an ephemeral port.
/// @return the listening fd, or -1 (bad host, port in use, ...).
int bind_tcp_listener(const char* host, int port, int* out_port);

} // namespace helix
