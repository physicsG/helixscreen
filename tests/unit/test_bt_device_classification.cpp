// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_bt_device_classification.cpp
 * @brief Tests for the is_scanner classification logic used in bt_discovery.cpp.
 *
 * bt_discovery.cpp classifies devices using:
 *   is_scanner = dominated_by_scanner && !dominated_by_uuid && !dominated_by_name
 *
 * These tests verify the decision matrix ensuring barcode scanners are correctly
 * identified and excluded from the label printer dropdown (#779).
 */

#include "bluetooth_plugin.h"
#include "bt_discovery_utils.h"
#include "bt_scanner_discovery_utils.h"

#include "../catch_amalgamated.hpp"

using namespace helix::bluetooth;

namespace {

/// Replicates the is_scanner classification from bt_discovery.cpp process_device_properties()
bool classify_is_scanner(const char* name, const char* const* uuids, int uuid_count) {
    bool has_printer_uuid = false;
    for (int i = 0; i < uuid_count; ++i) {
        if (is_label_printer_uuid(uuids[i])) {
            has_printer_uuid = true;
            break;
        }
    }

    bool is_printer_name = (name && name[0]) && is_likely_label_printer(name);

    bool is_scanner = false;
    for (int i = 0; i < uuid_count; ++i) {
        if (is_hid_scanner_uuid(uuids[i])) {
            is_scanner = true;
            break;
        }
    }
    if (!is_scanner && name && name[0]) {
        is_scanner = is_likely_bt_scanner(name);
    }

    return is_scanner && !has_printer_uuid && !is_printer_name;
}

} // namespace

// ============================================================================
// is_scanner classification matrix
// ============================================================================

TEST_CASE("classify_is_scanner - barcode scanner with HID UUID is scanner",
          "[bluetooth][scanner][classification]") {
    const char* uuids[] = {"00001124-0000-1000-8000-00805f9b34fb"}; // HID
    REQUIRE(classify_is_scanner("CT10 Barcode Scanner", uuids, 1));
}

TEST_CASE("classify_is_scanner - barcode scanner with HOGP UUID is scanner",
          "[bluetooth][scanner][classification]") {
    const char* uuids[] = {"00001812-0000-1000-8000-00805f9b34fb"}; // HOGP
    REQUIRE(classify_is_scanner("Wireless Scanner", uuids, 1));
}

TEST_CASE("classify_is_scanner - scanner name without HID UUID is scanner",
          "[bluetooth][scanner][classification]") {
    REQUIRE(classify_is_scanner("CT10 Barcode Scanner", nullptr, 0));
}

TEST_CASE("classify_is_scanner - label printer with SPP UUID is NOT scanner",
          "[bluetooth][scanner][classification]") {
    const char* uuids[] = {"00001101-0000-1000-8000-00805f9b34fb"}; // SPP
    REQUIRE_FALSE(classify_is_scanner("QL-820NWB", uuids, 1));
}

TEST_CASE("classify_is_scanner - label printer by name is NOT scanner",
          "[bluetooth][scanner][classification]") {
    REQUIRE_FALSE(classify_is_scanner("Brother QL-820NWB", nullptr, 0));
    REQUIRE_FALSE(classify_is_scanner("Phomemo M110", nullptr, 0));
    REQUIRE_FALSE(classify_is_scanner("Niimbot B21", nullptr, 0));
}

TEST_CASE("classify_is_scanner - device with both printer and scanner UUIDs prefers printer",
          "[bluetooth][scanner][classification]") {
    // A device that advertises both SPP (printer) and HID (scanner) UUIDs
    // should NOT be classified as a scanner — printer UUID takes priority
    const char* uuids[] = {
        "00001101-0000-1000-8000-00805f9b34fb", // SPP (printer)
        "00001124-0000-1000-8000-00805f9b34fb", // HID (scanner)
    };
    REQUIRE_FALSE(classify_is_scanner("Unknown Device", uuids, 2));
}

TEST_CASE("classify_is_scanner - scanner brand names detected",
          "[bluetooth][scanner][classification]") {
    REQUIRE(classify_is_scanner("Tera HW0002", nullptr, 0));
    REQUIRE(classify_is_scanner("Netum C750", nullptr, 0));
    REQUIRE(classify_is_scanner("Symcode MJ-2877", nullptr, 0));
    REQUIRE(classify_is_scanner("Inateck BCST-70", nullptr, 0));
    REQUIRE(classify_is_scanner("Eyoyo EY-015", nullptr, 0));
}

TEST_CASE("classify_is_scanner - generic device with no scanner traits is NOT scanner",
          "[bluetooth][scanner][classification]") {
    REQUIRE_FALSE(classify_is_scanner("Galaxy Buds Pro", nullptr, 0));
    REQUIRE_FALSE(classify_is_scanner("Logitech MX Master", nullptr, 0));
}

TEST_CASE("classify_is_scanner - paired device with HID UUID only is scanner",
          "[bluetooth][scanner][classification]") {
    // Scenario from bug report: paired barcode scanner shows up in label printer
    // settings because it passes dominated_by_paired AND dominated_by_scanner.
    // The is_scanner flag should be true so it gets filtered from the dropdown.
    const char* uuids[] = {"00001124-0000-1000-8000-00805f9b34fb"}; // HID
    REQUIRE(classify_is_scanner("SomeGenericName", uuids, 1));
}

// ============================================================================
// helix_bt_device struct has is_scanner field
// ============================================================================

TEST_CASE("helix_bt_device - zero-initialized has is_scanner false",
          "[bluetooth][scanner][classification]") {
    helix_bt_device dev = {};
    REQUIRE_FALSE(dev.is_scanner);
}

TEST_CASE("helix_bt_device - is_scanner can be set", "[bluetooth][scanner][classification]") {
    helix_bt_device dev = {};
    dev.is_scanner = true;
    REQUIRE(dev.is_scanner);
}
