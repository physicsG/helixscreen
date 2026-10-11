// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
//
// BLE connection table and handle semantics of the Bluetooth plugin, exercised
// on a context with no bus: disconnect skips its D-Bus calls when ctx->bus is
// null, so the table, the handle decoding and the reader wakeup run as they do
// on hardware.
//
// src/bluetooth/ is built into the plugin .so, not the app, so the sources are
// compiled into this translation unit. BusThread's definitions come from
// test_bt_bus_thread.cpp, which includes bt_bus_thread.cpp the same way.

#include "../catch_amalgamated.hpp"

#if defined(__has_include) && __has_include(<systemd/sd-bus.h>)

#include <chrono>
#include <fcntl.h>
#include <future>

#include "../../src/bluetooth/bt_agent.cpp"
#include "../../src/bluetooth/bt_ble.cpp"
#include "../../src/bluetooth/bt_discovery.cpp"
#include "../../src/bluetooth/bt_plugin.cpp"

using namespace std::chrono_literals;

namespace {

/// Puts an active, fd-less connection in the table the way connect_ble does.
int add_test_connection(helix_bt_context& ctx) {
    auto conn = std::make_shared<helix_bt_context::BleConnection>();
    conn->active = true;
    return ble_store_connection(&ctx, std::move(conn));
}

} // namespace

TEST_CASE("BLE read blocked on the notify queue wakes when the handle is disconnected",
          "[bt][ble][slow]") {
    helix_bt_context ctx;
    int handle = add_test_connection(ctx);

    auto reader = std::async(std::launch::async, [&] {
        uint8_t buf[16];
        return helix_bt_ble_read(&ctx, handle, buf, sizeof(buf), 5000);
    });

    std::this_thread::sleep_for(50ms);
    helix_bt_disconnect(&ctx, handle);

    REQUIRE(reader.wait_for(1s) == std::future_status::ready);
    REQUIRE(reader.get() == -ENOTCONN);
}

TEST_CASE("A disconnected BLE slot is reused by the next connection", "[bt][ble]") {
    helix_bt_context ctx;
    int first = add_test_connection(ctx);
    helix_bt_disconnect(&ctx, first);

    int second = add_test_connection(ctx);

    CHECK(second == first);
    CHECK(ctx.ble_connections.size() == 1);
}

TEST_CASE("A disconnected BLE handle reports not connected", "[bt][ble]") {
    helix_bt_context ctx;
    int handle = add_test_connection(ctx);
    helix_bt_disconnect(&ctx, handle);

    uint8_t buf[4] = {};
    CHECK(helix_bt_ble_write(&ctx, handle, buf, sizeof(buf)) == -ENOTCONN);
    CHECK(helix_bt_ble_read(&ctx, handle, buf, sizeof(buf), 0) == -ENOTCONN);
}

TEST_CASE("An RFCOMM fd numbered like a BLE handle disconnects as RFCOMM", "[bt][ble]") {
    helix_bt_context ctx;
    int fd = fcntl(STDERR_FILENO, F_DUPFD_CLOEXEC, 1000);
    if (fd < 0)
        SKIP("cannot open an fd numbered 1000 or higher here");
    ctx.rfcomm_fds.insert(fd);

    helix_bt_disconnect(&ctx, fd);

    CHECK(ctx.rfcomm_fds.count(fd) == 0);
    bool closed = fcntl(fd, F_GETFD) == -1;
    if (!closed)
        close(fd);
    CHECK(closed);
}

#endif // __has_include(<systemd/sd-bus.h>)
