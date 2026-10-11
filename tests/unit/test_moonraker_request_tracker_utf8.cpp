// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_moonraker_request_tracker_utf8.cpp
 * @brief Params holding invalid UTF-8 (a gcode script, filename or SSID) must
 *        neither throw out of send() nor strand a pending request.
 */

#include "../test_helpers/moonraker_request_tracker_test_access.h"
#include "hv/WebSocketClient.h"
#include "moonraker_request.h"
#include "moonraker_request_tracker.h"

#include "../catch_amalgamated.hpp"
#include "hv/json.hpp"

using namespace helix;

namespace {

// 0xC3 opens a two-byte sequence; 0x28 cannot continue it.
json invalid_utf8_params() {
    return json{{"script", std::string("M117 caf\xc3\x28")}};
}

} // namespace

TEST_CASE("send with invalid UTF-8 params does not throw or leak a pending request",
          "[moonraker][tracker][utf8]") {
    MoonrakerRequestTracker tracker;
    hv::WebSocketClient ws; // unconnected: send() reports failure

    bool error_cb_fired = false;
    RequestId id = 0;
    REQUIRE_NOTHROW(id = tracker.send(ws, "printer.gcode.script", invalid_utf8_params(), nullptr,
                                      [&](const MoonrakerError&) { error_cb_fired = true; }));
    REQUIRE(id == INVALID_REQUEST_ID);
    REQUIRE(error_cb_fired);
    REQUIRE(MoonrakerRequestTrackerTestAccess::pending_count(tracker) == 0);
}

TEST_CASE("fire-and-forget send with invalid UTF-8 params does not throw",
          "[moonraker][tracker][utf8]") {
    MoonrakerRequestTracker tracker;
    hv::WebSocketClient ws;

    int result = 0;
    REQUIRE_NOTHROW(
        result = tracker.send_fire_and_forget(ws, "printer.gcode.script", invalid_utf8_params()));
    REQUIRE(result < 0);
}
