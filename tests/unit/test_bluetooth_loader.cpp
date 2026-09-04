// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "bluetooth_loader.h"

#include "../catch_amalgamated.hpp"

using namespace helix::bluetooth;

TEST_CASE("BluetoothLoader - no crash when plugin missing", "[bluetooth]") {
    auto& loader = BluetoothLoader::instance();
    // On dev machines without the .so, is_available() should not crash
    REQUIRE_NOTHROW(loader.is_available());
}

TEST_CASE("BluetoothLoader - singleton consistency", "[bluetooth]") {
    auto& a = BluetoothLoader::instance();
    auto& b = BluetoothLoader::instance();
    REQUIRE(&a == &b);
}

TEST_CASE("BluetoothLoader - function pointers null when unavailable", "[bluetooth]") {
    auto& loader = BluetoothLoader::instance();
    if (!loader.is_available()) {
        REQUIRE(loader.init == nullptr);
        REQUIRE(loader.deinit == nullptr);
        REQUIRE(loader.discover == nullptr);
        REQUIRE(loader.connect_rfcomm == nullptr);
        REQUIRE(loader.connect_ble == nullptr);
    }
    // If available, function pointers should be non-null (tested on BT-capable machines)
}
