// Copyright (C) 2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_mock_klippy_uds.cpp
 * @brief The mock's klippy socket stays reachable however many co-location
 *        probes a process makes.
 */

#include "../lvgl_test_fixture.h"
#include "belt_stream_client.h"
#include "live_sweep_preview.h"
#include "moonraker_api_mock.h"
#include "moonraker_client_mock.h"
#include "printer_state.h"

#include <string>

#include "../catch_amalgamated.hpp"

TEST_CASE_METHOD(LVGLTestFixture, "mock klippy socket accepts every probe a long run makes",
                 "[belt][mock][mock_uds]") {
    MoonrakerClientMock client(MoonrakerClientMock::PrinterType::VORON_24);
    helix::PrinterState state;
    state.init_subjects(false);
    MoonrakerAPIMock api(client, state);

    // Each probe connects and hangs up, as the Belt Tension gate does. 300 is
    // past macOS's 128-entry listen backlog, the smallest a dev host has.
    int reachable = 0;
    constexpr int kProbes = 300;
    for (int i = 0; i < kProbes; ++i) {
        std::string path;
        helix::calibration::query_klippy_uds_address(api,
                                                     [&](std::string p) { path = std::move(p); });
        REQUIRE_FALSE(path.empty());
        if (helix::calibration::BeltStreamClient::socket_reachable(path)) {
            ++reachable;
        }
    }
    CHECK(reachable == kProbes);
}
