// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "connection_state.h"

#include <string>
#include <vector>

/**
 * @brief Validate IP address or hostname format
 *
 * Accepts:
 * - Valid IPv4 addresses (e.g., "192.168.1.1")
 * - Valid hostnames (e.g., "printer.local", "my-printer")
 *
 * @param host IP address or hostname string
 * @return true if valid, false otherwise
 */
bool is_valid_ip_or_hostname(const std::string& host);

/**
 * @brief Validate port number
 *
 * Accepts port numbers in range 1-65535 (numeric only)
 *
 * @param port_str Port number as string
 * @return true if valid, false otherwise
 */
bool is_valid_port(const std::string& port_str);

/**
 * @brief Strip non-numeric characters from a string
 *
 * Useful for sanitizing port input that may contain stray characters.
 *
 * @param str Input string (may be nullptr)
 * @return String containing only digit characters
 */
std::string sanitize_port(const char* str);
std::string sanitize_port(const std::string& str);

/**
 * @brief Resolve the Moonraker host to pre-fill in the setup wizard
 *
 * Config::get returns a stored value verbatim when the key exists, so a persisted
 * empty host — written by a factory reset or a --wizard re-run — comes back as ""
 * rather than falling through to a default. Treat an empty (or whitespace-only)
 * stored host as "not configured" and fall back to the localhost default, so the
 * wizard pre-fills 127.0.0.1 (the common same-host setup) instead of a blank field.
 * On Android, Moonraker is always remote, so the default is empty.
 *
 * @param stored_host The host string read from config (may be empty)
 * @param is_android  Whether running on the Android platform
 * @return stored_host if it is non-blank, else "127.0.0.1" (non-Android) or "" (Android)
 */
std::string resolve_moonraker_host_default(const std::string& stored_host, bool is_android);

namespace helix {

/**
 * @brief Whether a failed attempt's io error only says "never connected"
 *
 * A non-blocking connect that fails surfaces from getpeername(): ENOTCONN on
 * Linux, EINVAL on the BSDs (macOS). The socket's pending SO_ERROR then holds
 * the real cause, so it is read for these and only these.
 */
bool connect_failure_reads_socket_error(int io_error);

/**
 * @brief Classify why a connection attempt that never opened failed
 *
 * When connect_failure_reads_socket_error(io_error), the socket's pending
 * SO_ERROR is the cause; otherwise the io error stands.
 *
 * @param io_error     The transport's error for the closed socket (errno value, 0 if none)
 * @param socket_error The socket's pending SO_ERROR (0 if none or unread)
 */
ConnectFailureKind classify_connect_failure(int io_error, int socket_error);

/**
 * @brief The setup wizard's one-sentence hint for a failed connection test
 *
 * Returns an untranslated literal marked for extraction; callers pass it to lv_tr().
 * Only the Android timeout hint mentions mobile data: a phone off the printer's
 * Wi-Fi is the usual reason nothing answers there.
 */
const char* connect_failure_hint(ConnectFailureKind kind, bool is_android);

/// Moonraker's own default port, which the connection fields start from.
constexpr int kDefaultMoonrakerPort = 7125;
/// Where Moonraker answers when a web server such as nginx fronts it.
constexpr int kFallbackMoonrakerPort = 80;

/// Where the port field's value came from.
enum class PortOrigin {
    FilledIn,   ///< The app put it there: the default, or the printer's saved port
    Typed,      ///< The user edited the field
    Discovered, ///< Picked from the printers mDNS or the LAN sweep found
};

/**
 * @brief The ports a connection test tries for the port field's text, in order
 *
 * Only the untouched 7125 default is a guess, so only it falls back to port 80 when nothing
 * answers on 7125; a saved 7125 cannot be told from the default and counts as one. Any
 * other filled-in port (a saved one), a typed port and a discovered port are tried alone.
 * An empty field means the default.
 *
 * @param port_text The port field's text (digits; surrounding whitespace ignored)
 * @param origin    Where that text came from
 * @return The ports to try in order; empty when port_text is not a valid port
 */
std::vector<int> moonraker_ports_to_try(const std::string& port_text, PortOrigin origin);

} // namespace helix
