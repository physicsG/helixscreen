// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "bt_scanner_discovery_utils.h"

#include "../catch_amalgamated.hpp"

using namespace helix::bluetooth;

// ============================================================================
// is_hid_scanner_uuid
// ============================================================================

TEST_CASE("is_hid_scanner_uuid - matches classic HID UUID", "[bluetooth][scanner]") {
    REQUIRE(is_hid_scanner_uuid("00001124-0000-1000-8000-00805f9b34fb"));
}

TEST_CASE("is_hid_scanner_uuid - matches HID prefix only", "[bluetooth][scanner]") {
    REQUIRE(is_hid_scanner_uuid("00001124"));
}

TEST_CASE("is_hid_scanner_uuid - matches HID-over-GATT UUID", "[bluetooth][scanner]") {
    REQUIRE(is_hid_scanner_uuid("00001812-0000-1000-8000-00805f9b34fb"));
}

TEST_CASE("is_hid_scanner_uuid - matches HOGP prefix only", "[bluetooth][scanner]") {
    REQUIRE(is_hid_scanner_uuid("00001812"));
}

TEST_CASE("is_hid_scanner_uuid - case insensitive", "[bluetooth][scanner]") {
    REQUIRE(is_hid_scanner_uuid("00001124-0000-1000-8000-00805F9B34FB"));
    REQUIRE(is_hid_scanner_uuid("00001812-0000-1000-8000-00805F9B34FB"));
}

TEST_CASE("is_hid_scanner_uuid - rejects null", "[bluetooth][scanner]") {
    REQUIRE_FALSE(is_hid_scanner_uuid(nullptr));
}

TEST_CASE("is_hid_scanner_uuid - rejects empty string", "[bluetooth][scanner]") {
    REQUIRE_FALSE(is_hid_scanner_uuid(""));
}

TEST_CASE("is_hid_scanner_uuid - rejects label printer UUIDs", "[bluetooth][scanner]") {
    REQUIRE_FALSE(is_hid_scanner_uuid("00001101-0000-1000-8000-00805f9b34fb")); // SPP
    REQUIRE_FALSE(is_hid_scanner_uuid("0000ff00-0000-1000-8000-00805f9b34fb")); // Phomemo
}

TEST_CASE("is_hid_scanner_uuid - rejects random UUID", "[bluetooth][scanner]") {
    REQUIRE_FALSE(is_hid_scanner_uuid("deadbeef-1234-5678-9abc-def012345678"));
}

// ============================================================================
// is_likely_bt_scanner
// ============================================================================

TEST_CASE("is_likely_bt_scanner - matches 'barcode' in name", "[bluetooth][scanner]") {
    REQUIRE(is_likely_bt_scanner("CT10 Barcode Scanner"));
    REQUIRE(is_likely_bt_scanner("barcode reader"));
}

TEST_CASE("is_likely_bt_scanner - matches 'scanner' in name", "[bluetooth][scanner]") {
    REQUIRE(is_likely_bt_scanner("Wireless Scanner"));
    REQUIRE(is_likely_bt_scanner("BT scanner pro"));
}

TEST_CASE("is_likely_bt_scanner - matches known brands", "[bluetooth][scanner]") {
    REQUIRE(is_likely_bt_scanner("Tera HW0002"));
    REQUIRE(is_likely_bt_scanner("Netum C750"));
    REQUIRE(is_likely_bt_scanner("Symcode MJ-2877"));
    REQUIRE(is_likely_bt_scanner("Inateck BCST-70"));
    REQUIRE(is_likely_bt_scanner("Eyoyo EY-015"));
}

TEST_CASE("is_likely_bt_scanner - case insensitive", "[bluetooth][scanner]") {
    REQUIRE(is_likely_bt_scanner("TERA HW0002"));
    REQUIRE(is_likely_bt_scanner("BARCODE SCANNER"));
}

TEST_CASE("is_likely_bt_scanner - rejects null", "[bluetooth][scanner]") {
    REQUIRE_FALSE(is_likely_bt_scanner(nullptr));
}

TEST_CASE("is_likely_bt_scanner - rejects empty string", "[bluetooth][scanner]") {
    REQUIRE_FALSE(is_likely_bt_scanner(""));
}

TEST_CASE("is_likely_bt_scanner - rejects label printers", "[bluetooth][scanner]") {
    REQUIRE_FALSE(is_likely_bt_scanner("Brother QL-800"));
    REQUIRE_FALSE(is_likely_bt_scanner("Phomemo M110"));
    REQUIRE_FALSE(is_likely_bt_scanner("Niimbot B21"));
}

TEST_CASE("is_likely_bt_scanner - rejects generic keyboards", "[bluetooth][scanner]") {
    REQUIRE_FALSE(is_likely_bt_scanner("Logitech K380"));
    REQUIRE_FALSE(is_likely_bt_scanner("Apple Magic Keyboard"));
}
