// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

namespace helix {

/**
 * @brief Connection state for the Moonraker transport
 *
 * Lives in its own header so UI/observer code can bind to connection state
 * without including the concrete client (which drags libhv on desktop).
 */
enum class ConnectionState {
    DISCONNECTED, // Not connected
    CONNECTING,   // Connection in progress
    CONNECTED,    // Connected and ready
    RECONNECTING, // Automatic reconnection in progress
    FAILED        // Connection failed (max retries exceeded)
};

/**
 * @brief Why a connection attempt that never opened failed, as far as the socket can tell
 */
enum class ConnectFailureKind {
    Unknown, // nothing failed yet, or no socket-level cause (e.g. the WebSocket upgrade failed)
    Timeout, // no answer: the connect timed out or had no route to the host
    Refused, // the host answered and refused that port
};

} // namespace helix
