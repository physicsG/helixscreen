// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "klippy_state.h"
#include "moonraker_events.h"

#include <optional>
#include <string_view>

namespace helix {

enum class KlippyNotificationKind { Disconnected, Shutdown, Ready };

/// What a notify_klippy_* notification from Moonraker means for a client. Both the
/// desktop and ESP32 clients act on this, each through its own transport.
struct KlippyNotification {
    KlippyNotificationKind kind;
    KlippyState state;     ///< Klippy's state from here on: a live, authoritative report
    bool abandon_requests; ///< Klippy is gone, so nothing pending will be answered
    bool rediscover;       ///< Run discovery (and the connected callbacks) again
    MoonrakerEvent event;
};

/**
 * @param discovery_completed whether discovery has completed on this connection
 * @return nullopt for any method that is not a notify_klippy_* notification
 */
inline std::optional<KlippyNotification> classify_klippy_notification(std::string_view method,
                                                                      bool discovery_completed) {
    if (method == "notify_klippy_disconnected") {
        return KlippyNotification{
            KlippyNotificationKind::Disconnected, KlippyState::SHUTDOWN, true, false,
            MoonrakerEvent::translatable(
                MoonrakerEventType::KLIPPY_DISCONNECTED,
                TR_NOOP("Klipper has disconnected from Moonraker. Check for errors in your "
                        "printer interface."),
                {}, true)};
    }
    if (method == "notify_klippy_shutdown") {
        // Shutdown is a state discovery accepts. A connection that met Klippy while it
        // was unreachable, and deferred discovery, gets it now (#802).
        return KlippyNotification{
            KlippyNotificationKind::Shutdown, KlippyState::SHUTDOWN, false, !discovery_completed,
            MoonrakerEvent::translatable(MoonrakerEventType::KLIPPY_SHUTDOWN,
                                         TR_NOOP("Klipper has entered shutdown state."), {}, true)};
    }
    if (method == "notify_klippy_ready") {
        // Always rediscover: a FIRMWARE_RESTART after config edits can change the
        // hardware shape. The event routes to Ignore; the klippy_state observer carries
        // the user-facing ready signal.
        return KlippyNotification{
            KlippyNotificationKind::Ready, KlippyState::READY, false, true,
            MoonrakerEvent{MoonrakerEventType::KLIPPY_READY, "Klipper ready", {}, false}};
    }
    return std::nullopt;
}

} // namespace helix
