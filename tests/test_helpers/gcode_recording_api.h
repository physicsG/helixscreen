// Copyright (C) 2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "moonraker_api_mock.h"
#include "moonraker_error.h"

#include <set>
#include <string>
#include <utility>
#include <vector>

namespace helix::test {

/// Records every gcode a backend sends through IMoonrakerAPI and answers it
/// synchronously: success, Klipper's error for a command named in `fail`, a
/// request timeout for one named in `time_out`, or a dropped connection for one
/// named in `connection_lost`. With `hold_acks` set, a success
/// waits in `held_acks` until ack_held() sends it, the way a blocking macro's
/// answer arrives only when the macro finishes.
class GcodeRecordingApi : public MoonrakerAPIMock {
  public:
    using MoonrakerAPIMock::MoonrakerAPIMock;

    void execute_gcode(const std::string& gcode, SuccessCallback on_success, ErrorCallback on_error,
                       uint32_t timeout_ms = 0, bool /*silent*/ = false,
                       SuccessCallback /*on_queued*/ = nullptr,
                       bool /*caller_surfaces_errors*/ = true,
                       bool /*bypass_busy_gate*/ = false) override {
        sent.push_back(gcode);
        timeouts.push_back(timeout_ms);
        if (fail.count(gcode) != 0) {
            if (on_error) {
                MoonrakerError err;
                err.type = MoonrakerErrorType::UNKNOWN;
                err.message = "Klipper refused " + gcode;
                on_error(err);
            }
            return;
        }
        if (connection_lost.count(gcode) != 0) {
            if (on_error) {
                MoonrakerError err;
                err.type = MoonrakerErrorType::CONNECTION_LOST;
                err.message = "Connection lost: " + gcode;
                on_error(err);
            }
            return;
        }
        if (time_out.count(gcode) != 0) {
            if (on_error) {
                MoonrakerError err;
                err.type = MoonrakerErrorType::TIMEOUT;
                err.message = "Request timed out: " + gcode;
                on_error(err);
            }
            return;
        }
        if (hold_acks) {
            held_acks.push_back(std::move(on_success));
            return;
        }
        if (on_success) {
            on_success();
        }
    }

    /// Answer every held success, oldest first.
    void ack_held() {
        auto acks = std::move(held_acks);
        held_acks.clear();
        for (auto& ack : acks) {
            if (ack) {
                ack();
            }
        }
    }

    [[nodiscard]] bool contains(const std::string& needle) const {
        for (const auto& g : sent) {
            if (g.find(needle) != std::string::npos) {
                return true;
            }
        }
        return false;
    }

    std::set<std::string> fail;
    std::set<std::string> time_out;
    std::set<std::string> connection_lost;
    bool hold_acks = false;
    std::vector<SuccessCallback> held_acks;
    std::vector<std::string> sent;
    /// The timeout each send in `sent` asked for, in the same order.
    std::vector<uint32_t> timeouts;
};

} // namespace helix::test
