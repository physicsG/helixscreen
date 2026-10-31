// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "i_moonraker_client.h"
#include "moonraker_events.h"
#include "moonraker_manager.h"

#include <memory>
#include <mutex>

namespace helix {

class MoonrakerManagerTestAccess {
  public:
    static void present_event(MoonrakerManager& mgr, const MoonrakerEvent& evt) {
        mgr.present_event(evt);
    }

    /// Makes `client` the manager's transport and marks it initialized, as init() would,
    /// without building a mock printer or an API behind it.
    static void install_client(MoonrakerManager& mgr, std::unique_ptr<IMoonrakerClient> client) {
        mgr.m_client = std::move(client);
        mgr.m_initialized = true;
    }

    /// Whether a PrintStopReasonTracker is tapping the manager's client.
    static bool tracks_stop_reason(const MoonrakerManager& mgr) {
        return mgr.m_print_stop_reason != nullptr;
    }

    /// Queues a connection-state edge the way the client's state-change callback does,
    /// for the next process_notifications().
    static void queue_connection_state(MoonrakerManager& mgr, int old_state, int new_state) {
        nlohmann::json state_change;
        state_change["_connection_state"] = true;
        state_change["old_state"] = old_state;
        state_change["new_state"] = new_state;
        std::lock_guard<std::mutex> lock(mgr.m_notification_mutex);
        mgr.m_notification_queue.push({std::move(state_change), 0});
    }
};

} // namespace helix
