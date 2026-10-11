// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "moonraker_request.h"
#include "moonraker_request_tracker.h"

#include <mutex>
#include <utility>

// Friend of MoonrakerRequestTracker: seeds a pending request without a socket.
class MoonrakerRequestTrackerTestAccess {
  public:
    static void inject_request(helix::MoonrakerRequestTracker& tracker, helix::RequestId id,
                               ::PendingRequest request) {
        std::lock_guard<std::mutex> lock(tracker.requests_mutex_);
        tracker.pending_requests_[id] = std::move(request);
    }

    static size_t pending_count(helix::MoonrakerRequestTracker& tracker) {
        std::lock_guard<std::mutex> lock(tracker.requests_mutex_);
        return tracker.pending_requests_.size();
    }
};
