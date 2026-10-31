// Copyright (C) 2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "moonraker_auth_http.h"

#include "hv/HttpClient.h"
#include "moonraker_auth.h"

#include <utility>

namespace helix::moonraker_auth {

void apply_to_request(HttpRequest& req) {
    HeaderDecision decision = header_action(req.host, req.port, req.GetHeader(kApiKeyHeader));
    switch (decision.action) {
    case HeaderAction::Set:
        req.headers[kApiKeyHeader] = std::move(decision.key);
        break;
    case HeaderAction::Remove:
        req.headers.erase(kApiKeyHeader);
        break;
    case HeaderAction::Leave:
        break;
    }
}

void install_http_request_hook() {
    hv_set_http_request_hook([](HttpRequest* req) {
        if (req) {
            apply_to_request(*req);
        }
    });
}

} // namespace helix::moonraker_auth
