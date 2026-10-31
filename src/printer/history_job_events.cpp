// SPDX-License-Identifier: GPL-3.0-or-later

#include "history_job_events.h"

#include "i_moonraker_api.h"
#include "i_moonraker_client.h"
#include "json_utils.h"

#include <utility>

namespace helix {

std::optional<HistoryJobEvent> parse_history_job_event(const nlohmann::json& msg) {
    const nlohmann::json* payload = json_util::notification_payload(msg);
    if (payload == nullptr) {
        return std::nullopt;
    }
    HistoryJobEvent event;
    event.action = json_util::safe_string(*payload, "action");
    if (event.action != "added" && event.action != "finished") {
        return std::nullopt;
    }
    const nlohmann::json* job = json_util::find_member(*payload, "job");
    if (job == nullptr || !job->is_object()) {
        return std::nullopt;
    }
    event.job_id = json_util::safe_string(*job, "job_id");
    if (event.job_id.empty()) {
        return std::nullopt;
    }
    event.filename = json_util::safe_string(*job, "filename");
    event.status = json_util::safe_string(*job, "status");
    event.start_time = json_util::safe_double(*job, "start_time");
    return event;
}

void HistoryJobEvents::subscribe(IMoonrakerAPI& api, const char* handler_name, LifetimeToken token,
                                 std::function<void(const HistoryJobEvent&)> on_event) {
    IMoonrakerClient& client = api.get_client();
    if (client_ == &client) {
        return;
    }
    client_ = &client;
    client.register_method_callback(
        "notify_history_changed", handler_name,
        [token, on_event = std::move(on_event)](const nlohmann::json& data) {
            // WebSocket thread: parse only.
            std::optional<HistoryJobEvent> event = parse_history_job_event(data);
            if (!event) {
                return;
            }
            token.defer("HistoryJobEvents::event",
                        [on_event, event = std::move(*event)] { on_event(event); });
        });
}

} // namespace helix
