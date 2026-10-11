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
/// synchronously: success, or Klipper's error for a command named in `fail`.
class GcodeRecordingApi : public MoonrakerAPIMock {
  public:
    using MoonrakerAPIMock::MoonrakerAPIMock;

    void execute_gcode(const std::string& gcode, SuccessCallback on_success, ErrorCallback on_error,
                       uint32_t /*timeout_ms*/ = 0, bool /*silent*/ = false,
                       SuccessCallback /*on_queued*/ = nullptr,
                       bool /*caller_surfaces_errors*/ = true,
                       bool /*bypass_busy_gate*/ = false) override {
        sent.push_back(gcode);
        if (fail.count(gcode) != 0) {
            if (on_error) {
                MoonrakerError err;
                err.type = MoonrakerErrorType::UNKNOWN;
                err.message = "Klipper refused " + gcode;
                on_error(err);
            }
            return;
        }
        if (on_success) {
            on_success();
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
    std::vector<std::string> sent;
};

} // namespace helix::test
