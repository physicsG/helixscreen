// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_history_page_oom.cpp
 * @brief A history page too big for memory is an error, not an abort
 */

#include "../test_helpers/scoped_try_reserve_failure.h"
#include "moonraker_client_mock.h"
#include "moonraker_history_api.h"
#include "try_reserve.h"

#include "../catch_amalgamated.hpp"

TEST_CASE("a history page that cannot be allocated reports an error", "[history][oom]") {
    MoonrakerClientMock client(MoonrakerClientMock::PrinterType::VORON_24);
    client.connect("ws://mock/websocket", []() {}, []() {});
    MoonrakerHistoryAPI history(client);

    int pages = 0;
    std::string error;
    {
        helix::ScopedTryReserveFailure no_memory;
        history.get_history_list(
            50, 0, 0.0, 0.0, [&pages](const std::vector<PrintHistoryJob>&, uint64_t) { ++pages; },
            [&error](const MoonrakerError& e) { error = e.message; });
    }

    CHECK(pages == 0);
    CHECK(error.find("memory") != std::string::npos);
    client.disconnect();
}
