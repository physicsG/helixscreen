// Copyright (C) 2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "hv/HttpMessage.h"

/// The libhv side of moonraker_auth: every HTTP request libhv sends passes
/// through apply_to_request(), so a Moonraker URL built anywhere carries the key.
namespace helix::moonraker_auth {

/// Sets, keeps or strips @p req's X-Api-Key as header_action() decides for its
/// parsed host and port.
void apply_to_request(HttpRequest& req);

/// Installs apply_to_request() as libhv's request hook. Idempotent.
void install_http_request_hook();

} // namespace helix::moonraker_auth
