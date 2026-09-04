// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_bt_discovery_utils.cpp
 * @brief Unit tests for Bluetooth discovery UUID classification helpers.
 *
 * Tests is_label_printer_uuid(), uuid_is_ble(), and is_likely_label_printer()
 * from bt_discovery_utils.h:
 *   - SPP UUID prefix "00001101" (Brother QL Classic)
 *   - Phomemo BLE UUID prefix "0000ff00"
 *   - Device name matching for known label printer brands
 *   - Case insensitivity, null safety, rejection of unknown UUIDs/names
 */

#include "bt_discovery_utils.h"

#include "../catch_amalgamated.hpp"

using namespace helix::bluetooth;

// ============================================================================
// is_label_printer_uuid
// ============================================================================

TEST_CASE("is_label_printer_uuid - matches full SPP UUID", "[bluetooth][discovery]") {
    REQUIRE(is_label_printer_uuid("00001101-0000-1000-8000-00805f9b34fb"));
}

TEST_CASE("is_label_printer_uuid - matches SPP UUID prefix only", "[bluetooth][discovery]") {
    REQUIRE(is_label_printer_uuid("00001101"));
}

TEST_CASE("is_label_printer_uuid - matches full Phomemo BLE UUID", "[bluetooth][discovery]") {
    REQUIRE(is_label_printer_uuid("0000ff00-0000-1000-8000-00805f9b34fb"));
}

TEST_CASE("is_label_printer_uuid - matches Phomemo BLE prefix only", "[bluetooth][discovery]") {
    REQUIRE(is_label_printer_uuid("0000ff00"));
}

TEST_CASE("is_label_printer_uuid - case insensitive SPP", "[bluetooth][discovery]") {
    REQUIRE(is_label_printer_uuid("00001101-0000-1000-8000-00805F9B34FB"));
}

TEST_CASE("is_label_printer_uuid - case insensitive Phomemo", "[bluetooth][discovery]") {
    REQUIRE(is_label_printer_uuid("0000FF00-0000-1000-8000-00805f9b34fb"));
    REQUIRE(is_label_printer_uuid("0000Ff00-0000-1000-8000-00805f9b34fb"));
}

TEST_CASE("is_label_printer_uuid - rejects null", "[bluetooth][discovery]") {
    REQUIRE_FALSE(is_label_printer_uuid(nullptr));
}

TEST_CASE("is_label_printer_uuid - rejects empty string", "[bluetooth][discovery]") {
    REQUIRE_FALSE(is_label_printer_uuid(""));
}

TEST_CASE("is_label_printer_uuid - rejects random UUID", "[bluetooth][discovery]") {
    // A2DP audio sink UUID
    REQUIRE_FALSE(is_label_printer_uuid("0000110b-0000-1000-8000-00805f9b34fb"));
}

TEST_CASE("is_label_printer_uuid - rejects short non-matching string", "[bluetooth][discovery]") {
    REQUIRE_FALSE(is_label_printer_uuid("0000"));
    REQUIRE_FALSE(is_label_printer_uuid("abcdefgh"));
}

TEST_CASE("is_label_printer_uuid - rejects partial prefix match", "[bluetooth][discovery]") {
    // Starts with 0000110 but 8th char is '0' not '1'
    REQUIRE_FALSE(is_label_printer_uuid("00001100-0000-1000-8000-00805f9b34fb"));
}

// ============================================================================
// uuid_is_ble
// ============================================================================

TEST_CASE("uuid_is_ble - Phomemo UUID is BLE", "[bluetooth][discovery]") {
    REQUIRE(uuid_is_ble("0000ff00-0000-1000-8000-00805f9b34fb"));
}

TEST_CASE("uuid_is_ble - Phomemo prefix only is BLE", "[bluetooth][discovery]") {
    REQUIRE(uuid_is_ble("0000ff00"));
}

TEST_CASE("uuid_is_ble - case insensitive", "[bluetooth][discovery]") {
    REQUIRE(uuid_is_ble("0000FF00-0000-1000-8000-00805f9b34fb"));
}

TEST_CASE("uuid_is_ble - SPP UUID is not BLE", "[bluetooth][discovery]") {
    REQUIRE_FALSE(uuid_is_ble("00001101-0000-1000-8000-00805f9b34fb"));
}

TEST_CASE("uuid_is_ble - null returns false", "[bluetooth][discovery]") {
    REQUIRE_FALSE(uuid_is_ble(nullptr));
}

TEST_CASE("uuid_is_ble - empty string returns false", "[bluetooth][discovery]") {
    REQUIRE_FALSE(uuid_is_ble(""));
}

TEST_CASE("uuid_is_ble - random UUID returns false", "[bluetooth][discovery]") {
    REQUIRE_FALSE(uuid_is_ble("0000110b-0000-1000-8000-00805f9b34fb"));
}

// ============================================================================
// is_likely_label_printer — device name heuristics
// ============================================================================

TEST_CASE("is_likely_label_printer - null and empty", "[bluetooth][discovery]") {
    REQUIRE_FALSE(is_likely_label_printer(nullptr));
    REQUIRE_FALSE(is_likely_label_printer(""));
}

TEST_CASE("is_likely_label_printer - Brother QL series", "[bluetooth][discovery]") {
    REQUIRE(is_likely_label_printer("QL-800"));
    REQUIRE(is_likely_label_printer("QL-820NWB"));
    REQUIRE(is_likely_label_printer("ql-700")); // case insensitive prefix
}

TEST_CASE("is_likely_label_printer - Brother PT series", "[bluetooth][discovery]") {
    REQUIRE(is_likely_label_printer("PT-P950NW"));
    REQUIRE(is_likely_label_printer("PT-D610BT"));
}

TEST_CASE("is_likely_label_printer - Brother TD series", "[bluetooth][discovery]") {
    REQUIRE(is_likely_label_printer("TD-4550DNWB"));
}

TEST_CASE("is_likely_label_printer - Brother RJ series", "[bluetooth][discovery]") {
    REQUIRE(is_likely_label_printer("RJ-4250WB"));
}

TEST_CASE("is_likely_label_printer - Phomemo by name prefix", "[bluetooth][discovery]") {
    REQUIRE(is_likely_label_printer("Phomemo M110"));
    REQUIRE(is_likely_label_printer("phomemo")); // case insensitive
}

TEST_CASE("is_likely_label_printer - Phomemo model codes", "[bluetooth][discovery]") {
    REQUIRE(is_likely_label_printer("M110"));
    REQUIRE(is_likely_label_printer("M02"));
    REQUIRE(is_likely_label_printer("Q30"));
}

TEST_CASE("is_likely_label_printer - Dymo", "[bluetooth][discovery]") {
    REQUIRE(is_likely_label_printer("LW-600P"));
    REQUIRE(is_likely_label_printer("DYMO LabelWriter"));
}

TEST_CASE("is_likely_label_printer - MUNBYN", "[bluetooth][discovery]") {
    REQUIRE(is_likely_label_printer("MUNBYN ITPP941"));
}

TEST_CASE("is_likely_label_printer - Niimbot", "[bluetooth][discovery]") {
    REQUIRE(is_likely_label_printer("Niimbot B21"));
    REQUIRE(is_likely_label_printer("B21"));
    REQUIRE(is_likely_label_printer("D11"));
    REQUIRE(is_likely_label_printer("D110"));
}

TEST_CASE("is_likely_label_printer - generic keyword printer", "[bluetooth][discovery]") {
    REQUIRE(is_likely_label_printer("My BT Printer"));
    REQUIRE(is_likely_label_printer("THERMAL PRINTER"));
}

TEST_CASE("is_likely_label_printer - generic keyword label", "[bluetooth][discovery]") {
    REQUIRE(is_likely_label_printer("Bluetooth Label Maker"));
    REQUIRE(is_likely_label_printer("LABEL WRITER 450"));
}

TEST_CASE("is_likely_label_printer - rejects unrelated devices", "[bluetooth][discovery]") {
    REQUIRE_FALSE(is_likely_label_printer("Galaxy Buds Pro"));
    REQUIRE_FALSE(is_likely_label_printer("Logitech MX Master"));
    REQUIRE_FALSE(is_likely_label_printer("JBL Flip 5"));
    REQUIRE_FALSE(is_likely_label_printer("AirPods"));
    REQUIRE_FALSE(is_likely_label_printer("Xbox Controller"));
}

TEST_CASE("is_likely_label_printer - M/Q without digit is not matched", "[bluetooth][discovery]") {
    // M or Q followed by non-digit should not match the Phomemo model pattern
    REQUIRE_FALSE(is_likely_label_printer("MacBook"));
    REQUIRE_FALSE(is_likely_label_printer("Quiet Fan"));
}

// ============================================================================
// is_label_printer_uuid — additional edge cases
// ============================================================================

TEST_CASE("is_label_printer_uuid - mixed case prefix chars match SPP", "[bluetooth][discovery]") {
    // The first 8 chars are compared case-insensitively; 'A'-'F' in various cases
    REQUIRE(is_label_printer_uuid("00001101-ABCD-1000-8000-00805f9b34fb"));
    REQUIRE(is_label_printer_uuid("00001101-abcd-1000-8000-00805f9b34fb"));
}

TEST_CASE("is_label_printer_uuid - mixed case prefix chars match Phomemo",
          "[bluetooth][discovery]") {
    REQUIRE(is_label_printer_uuid("0000FF00-ABCD-1000-8000-00805f9b34fb"));
    REQUIRE(is_label_printer_uuid("0000fF00-abcd-1000-8000-00805f9b34fb"));
}

TEST_CASE("is_label_printer_uuid - strings shorter than 8 chars always reject",
          "[bluetooth][discovery]") {
    REQUIRE_FALSE(is_label_printer_uuid("0"));
    REQUIRE_FALSE(is_label_printer_uuid("0000110")); // 7 chars — one short of SPP prefix
    REQUIRE_FALSE(is_label_printer_uuid("0000ff0")); // 7 chars — one short of Phomemo prefix
    REQUIRE_FALSE(is_label_printer_uuid("1234567")); // 7 chars — unrelated
}

TEST_CASE("is_label_printer_uuid - exact 8-char SPP prefix without dash suffix matches",
          "[bluetooth][discovery]") {
    // The function only inspects the first 8 chars; no dash is required
    REQUIRE(is_label_printer_uuid("00001101"));
}

TEST_CASE("is_label_printer_uuid - exact 8-char Phomemo prefix without dash suffix matches",
          "[bluetooth][discovery]") {
    REQUIRE(is_label_printer_uuid("0000ff00"));
}

TEST_CASE("is_label_printer_uuid - partial prefix match off by one digit rejects",
          "[bluetooth][discovery]") {
    // Differs at position 7: '1' vs '2'
    REQUIRE_FALSE(is_label_printer_uuid("00001102-0000-1000-8000-00805f9b34fb"));
    // Differs at position 7: '0' vs '1' in Phomemo prefix
    REQUIRE_FALSE(is_label_printer_uuid("0000ff01-0000-1000-8000-00805f9b34fb"));
}

TEST_CASE("is_label_printer_uuid - unrelated 8-char strings reject", "[bluetooth][discovery]") {
    REQUIRE_FALSE(is_label_printer_uuid("abcdefgh"));
    REQUIRE_FALSE(is_label_printer_uuid("12345678"));
    REQUIRE_FALSE(is_label_printer_uuid("FFFFFFFF"));
}

// ============================================================================
// uuid_is_ble — additional edge cases
// ============================================================================

TEST_CASE("uuid_is_ble - mixed case Phomemo prefix matches", "[bluetooth][discovery]") {
    REQUIRE(uuid_is_ble("0000Ff00-0000-1000-8000-00805f9b34fb"));
    REQUIRE(uuid_is_ble("0000fF00"));
}

TEST_CASE("uuid_is_ble - strings shorter than 8 chars reject", "[bluetooth][discovery]") {
    REQUIRE_FALSE(uuid_is_ble("0000ff0")); // 7 chars
    REQUIRE_FALSE(uuid_is_ble("0000"));
    REQUIRE_FALSE(uuid_is_ble("0"));
}

TEST_CASE("uuid_is_ble - exact 8-char Phomemo prefix without dash matches",
          "[bluetooth][discovery]") {
    REQUIRE(uuid_is_ble("0000ff00"));
}

TEST_CASE("uuid_is_ble - partial Phomemo prefix off by one rejects", "[bluetooth][discovery]") {
    REQUIRE_FALSE(uuid_is_ble("0000ff01-0000-1000-8000-00805f9b34fb"));
    REQUIRE_FALSE(uuid_is_ble("0000fe00-0000-1000-8000-00805f9b34fb"));
}

TEST_CASE("uuid_is_ble - unrelated non-empty strings reject", "[bluetooth][discovery]") {
    REQUIRE_FALSE(uuid_is_ble("abcdefgh"));
    REQUIRE_FALSE(uuid_is_ble("FFFFFFFF-0000-0000-0000-000000000000"));
}

// ============================================================================
// is_likely_label_printer — case insensitivity
// ============================================================================

TEST_CASE("is_likely_label_printer - QL prefix case insensitive variants",
          "[bluetooth][discovery]") {
    REQUIRE(is_likely_label_printer("QL-820NWB"));
    REQUIRE(is_likely_label_printer("ql-820nwb"));
    REQUIRE(is_likely_label_printer("Ql-820NWB"));
    REQUIRE(is_likely_label_printer("qL-700"));
}

TEST_CASE("is_likely_label_printer - PT prefix case insensitive variants",
          "[bluetooth][discovery]") {
    REQUIRE(is_likely_label_printer("PT-P710BT"));
    REQUIRE(is_likely_label_printer("pt-p710bt"));
    REQUIRE(is_likely_label_printer("Pt-P710BT"));
}

TEST_CASE("is_likely_label_printer - TD prefix case insensitive variants",
          "[bluetooth][discovery]") {
    REQUIRE(is_likely_label_printer("TD-4550DNWB"));
    REQUIRE(is_likely_label_printer("td-4550dnwb"));
    REQUIRE(is_likely_label_printer("Td-4550DNWB"));
}

TEST_CASE("is_likely_label_printer - RJ prefix case insensitive variants",
          "[bluetooth][discovery]") {
    REQUIRE(is_likely_label_printer("RJ-4250WB"));
    REQUIRE(is_likely_label_printer("rj-4250wb"));
    REQUIRE(is_likely_label_printer("Rj-4250WB"));
}

TEST_CASE("is_likely_label_printer - Phomemo keyword case insensitive", "[bluetooth][discovery]") {
    REQUIRE(is_likely_label_printer("PHOMEMO"));
    REQUIRE(is_likely_label_printer("phomemo"));
    REQUIRE(is_likely_label_printer("PhOmEmO M110"));
    REQUIRE(is_likely_label_printer("PHOMEMO M02S"));
}

TEST_CASE("is_likely_label_printer - DYMO keyword case insensitive", "[bluetooth][discovery]") {
    REQUIRE(is_likely_label_printer("dymo"));
    REQUIRE(is_likely_label_printer("Dymo LabelWriter"));
    REQUIRE(is_likely_label_printer("DYMO LW-550"));
    REQUIRE(is_likely_label_printer("dYmO LW-550"));
}

TEST_CASE("is_likely_label_printer - LW prefix case insensitive", "[bluetooth][discovery]") {
    REQUIRE(is_likely_label_printer("LW-550"));
    REQUIRE(is_likely_label_printer("lw-550"));
    REQUIRE(is_likely_label_printer("Lw-600P"));
}

TEST_CASE("is_likely_label_printer - MUNBYN keyword case insensitive", "[bluetooth][discovery]") {
    REQUIRE(is_likely_label_printer("munbyn"));
    REQUIRE(is_likely_label_printer("Munbyn ITPP941"));
    REQUIRE(is_likely_label_printer("MUNBYN"));
}

TEST_CASE("is_likely_label_printer - Niimbot keyword case insensitive", "[bluetooth][discovery]") {
    REQUIRE(is_likely_label_printer("niimbot"));
    REQUIRE(is_likely_label_printer("NIIMBOT B21"));
    REQUIRE(is_likely_label_printer("NiImBoT D11"));
}

// ============================================================================
// is_likely_label_printer — near-misses that must NOT match
// ============================================================================

TEST_CASE("is_likely_label_printer - single letters alone do not match", "[bluetooth][discovery]") {
    REQUIRE_FALSE(is_likely_label_printer("M"));
    REQUIRE_FALSE(is_likely_label_printer("Q"));
    REQUIRE_FALSE(is_likely_label_printer("B"));
    REQUIRE_FALSE(is_likely_label_printer("m"));
    REQUIRE_FALSE(is_likely_label_printer("q"));
}

TEST_CASE("is_likely_label_printer - M/Q followed by non-digit do not match",
          "[bluetooth][discovery]") {
    REQUIRE_FALSE(is_likely_label_printer("Ma"));
    REQUIRE_FALSE(is_likely_label_printer("Qa"));
    REQUIRE_FALSE(is_likely_label_printer("M-110")); // hyphen is not a digit
    REQUIRE_FALSE(is_likely_label_printer("Q-30"));
    REQUIRE_FALSE(is_likely_label_printer("M ")); // space is not a digit
}

TEST_CASE("is_likely_label_printer - QL without dash does not match", "[bluetooth][discovery]") {
    // The prefix check is "QL-" (3 chars including dash); bare "QL" must not match
    REQUIRE_FALSE(is_likely_label_printer("QL"));
    REQUIRE_FALSE(is_likely_label_printer("ql"));
    REQUIRE_FALSE(is_likely_label_printer("QL800")); // no dash
}

TEST_CASE("is_likely_label_printer - PT/TD/RJ without dash do not match",
          "[bluetooth][discovery]") {
    REQUIRE_FALSE(is_likely_label_printer("PT"));
    REQUIRE_FALSE(is_likely_label_printer("PTX710")); // no dash
    REQUIRE_FALSE(is_likely_label_printer("TD"));
    REQUIRE_FALSE(is_likely_label_printer("TD4550")); // no dash
    REQUIRE_FALSE(is_likely_label_printer("RJ"));
    REQUIRE_FALSE(is_likely_label_printer("RJ4250")); // no dash
}

TEST_CASE("is_likely_label_printer - B2 alone is too short for B21", "[bluetooth][discovery]") {
    // "B21" prefix check requires exactly 3 chars; "B2" is only 2
    REQUIRE_FALSE(is_likely_label_printer("B2"));
    REQUIRE_FALSE(is_likely_label_printer("B2X")); // third char is not '1'
}

TEST_CASE("is_likely_label_printer - whitespace-only name does not match",
          "[bluetooth][discovery]") {
    REQUIRE_FALSE(is_likely_label_printer(" "));
    REQUIRE_FALSE(is_likely_label_printer("   "));
    REQUIRE_FALSE(is_likely_label_printer("\t"));
}

// ============================================================================
// is_likely_label_printer — Niimbot models with suffixes
// ============================================================================

TEST_CASE("is_likely_label_printer - Niimbot B21 with suffixes", "[bluetooth][discovery]") {
    REQUIRE(is_likely_label_printer("B21-A4F3"));
    REQUIRE(is_likely_label_printer("B21 Pro"));
    REQUIRE(is_likely_label_printer("b21-xxxx"));
}

TEST_CASE("is_likely_label_printer - Niimbot D11 with suffixes", "[bluetooth][discovery]") {
    REQUIRE(is_likely_label_printer("D11-A4F3"));
    REQUIRE(is_likely_label_printer("D11 Plus"));
    REQUIRE(is_likely_label_printer("d11"));
}

TEST_CASE("is_likely_label_printer - Niimbot D110 with suffixes", "[bluetooth][discovery]") {
    REQUIRE(is_likely_label_printer("D110-A4F3"));
    REQUIRE(is_likely_label_printer("D110 Pro"));
    REQUIRE(is_likely_label_printer("d110"));
}

// ============================================================================
// is_likely_label_printer — Supvan / KataSymbol models with suffixes
// ============================================================================

TEST_CASE("is_likely_label_printer - Supvan keyword and models", "[bluetooth][discovery]") {
    REQUIRE(is_likely_label_printer("Supvan E10"));
    REQUIRE(is_likely_label_printer("supvan"));
    REQUIRE(is_likely_label_printer("SUPVAN T50M"));
}

TEST_CASE("is_likely_label_printer - KataSymbol keyword", "[bluetooth][discovery]") {
    REQUIRE(is_likely_label_printer("KataSymbol E16"));
    REQUIRE(is_likely_label_printer("katasymbol"));
    REQUIRE(is_likely_label_printer("KATASYMBOL T50M"));
}

TEST_CASE("is_likely_label_printer - E10 with suffixes", "[bluetooth][discovery]") {
    REQUIRE(is_likely_label_printer("E10-B1C2"));
    REQUIRE(is_likely_label_printer("E10 Plus"));
    REQUIRE(is_likely_label_printer("e10"));
}

TEST_CASE("is_likely_label_printer - E16 with suffixes", "[bluetooth][discovery]") {
    REQUIRE(is_likely_label_printer("E16-B1C2"));
    REQUIRE(is_likely_label_printer("E16 Pro"));
    REQUIRE(is_likely_label_printer("e16"));
}

TEST_CASE("is_likely_label_printer - T50M with suffixes", "[bluetooth][discovery]") {
    REQUIRE(is_likely_label_printer("T50M-B1C2"));
    REQUIRE(is_likely_label_printer("T50M Pro"));
    REQUIRE(is_likely_label_printer("t50m"));
}

// ============================================================================
// is_likely_label_printer — generic keyword matches
// ============================================================================

TEST_CASE("is_likely_label_printer - keyword 'label' alone matches", "[bluetooth][discovery]") {
    REQUIRE(is_likely_label_printer("label"));
    REQUIRE(is_likely_label_printer("LABEL"));
    REQUIRE(is_likely_label_printer("Label"));
}

TEST_CASE("is_likely_label_printer - keyword 'label' embedded in longer name",
          "[bluetooth][discovery]") {
    REQUIRE(is_likely_label_printer("My Label Maker"));
    REQUIRE(is_likely_label_printer("BT Label Writer"));
    REQUIRE(is_likely_label_printer("BLUETOOTH LABEL"));
    REQUIRE(is_likely_label_printer("label_device_01"));
}

TEST_CASE("is_likely_label_printer - keyword 'printer' case insensitive",
          "[bluetooth][discovery]") {
    REQUIRE(is_likely_label_printer("Thermal Printer XYZ"));
    REQUIRE(is_likely_label_printer("thermal printer xyz"));
    REQUIRE(is_likely_label_printer("PRINTER"));
    REQUIRE(is_likely_label_printer("My Printer"));
}

// ============================================================================
// is_likely_label_printer — devices that must be filtered (not label printers)
// ============================================================================

TEST_CASE("is_likely_label_printer - smart home devices rejected", "[bluetooth][discovery]") {
    REQUIRE_FALSE(is_likely_label_printer("Philips Hue"));
    REQUIRE_FALSE(is_likely_label_printer("August Smart Lock"));
    REQUIRE_FALSE(is_likely_label_printer("Nest Thermostat"));
    REQUIRE_FALSE(is_likely_label_printer("Ring Doorbell"));
}

TEST_CASE("is_likely_label_printer - audio devices rejected", "[bluetooth][discovery]") {
    REQUIRE_FALSE(is_likely_label_printer("AirPods"));
    REQUIRE_FALSE(is_likely_label_printer("Galaxy Buds"));
    REQUIRE_FALSE(is_likely_label_printer("JBL Speaker"));
    REQUIRE_FALSE(is_likely_label_printer("Bose QC45"));
    REQUIRE_FALSE(is_likely_label_printer("Sony WH-1000XM5"));
}

TEST_CASE("is_likely_label_printer - trackers and misc BT devices rejected",
          "[bluetooth][discovery]") {
    REQUIRE_FALSE(is_likely_label_printer("Tile Tracker"));
    REQUIRE_FALSE(is_likely_label_printer("AirTag"));
    REQUIRE_FALSE(is_likely_label_printer("Galaxy SmartTag"));
}

TEST_CASE("is_likely_label_printer - MAC-like random names rejected", "[bluetooth][discovery]") {
    REQUIRE_FALSE(is_likely_label_printer("AA:BB:CC:DD:EE:FF"));
    REQUIRE_FALSE(is_likely_label_printer("00:11:22:33:44:55"));
    REQUIRE_FALSE(is_likely_label_printer("Unknown Device"));
}

TEST_CASE("is_likely_label_printer - very long unrelated name rejected", "[bluetooth][discovery]") {
    // Long string with no keywords, known brand prefixes, or model codes
    REQUIRE_FALSE(is_likely_label_printer(
        "This is a very long Bluetooth device name that should not match any heuristic "
        "because it does not contain any of the relevant brand names or model codes at all"));
}

TEST_CASE("is_likely_label_printer - Brother model numbers full table", "[bluetooth][discovery]") {
    // Spot-check a variety of real Brother models
    REQUIRE(is_likely_label_printer("QL-700"));
    REQUIRE(is_likely_label_printer("QL-800"));
    REQUIRE(is_likely_label_printer("QL-810W"));
    REQUIRE(is_likely_label_printer("QL-820NWB"));
    REQUIRE(is_likely_label_printer("QL-1100NWB"));
    REQUIRE(is_likely_label_printer("PT-P710BT"));
    REQUIRE(is_likely_label_printer("PT-P900W"));
    REQUIRE(is_likely_label_printer("PT-P950NW"));
    REQUIRE(is_likely_label_printer("TD-4550DNWB"));
    REQUIRE(is_likely_label_printer("TD-4650TNWB"));
    REQUIRE(is_likely_label_printer("RJ-4250WB"));
    REQUIRE(is_likely_label_printer("RJ-3250WB"));
}

TEST_CASE("is_likely_label_printer - Dymo model table", "[bluetooth][discovery]") {
    REQUIRE(is_likely_label_printer("LW-550"));
    REQUIRE(is_likely_label_printer("LW-600P"));
    REQUIRE(is_likely_label_printer("LW-450"));
    REQUIRE(is_likely_label_printer("DYMO LabelWriter 550"));
    REQUIRE(is_likely_label_printer("DYMO LabelWriter Turbo"));
    REQUIRE(is_likely_label_printer("Dymo LW-550"));
}

// ============================================================================
// name_suggests_ble
// ============================================================================

using helix::bluetooth::name_suggests_ble;

TEST_CASE("name_suggests_ble - null and empty", "[bluetooth][discovery]") {
    REQUIRE_FALSE(name_suggests_ble(nullptr));
    REQUIRE_FALSE(name_suggests_ble(""));
}

TEST_CASE("name_suggests_ble - Phomemo devices are NOT BLE (need SPP for printing)",
          "[bluetooth][discovery]") {
    REQUIRE_FALSE(name_suggests_ble("M110"));
    REQUIRE_FALSE(name_suggests_ble("M120"));
    REQUIRE_FALSE(name_suggests_ble("Q199E45E0775722"));
    REQUIRE_FALSE(name_suggests_ble("Q30"));
    REQUIRE_FALSE(name_suggests_ble("Phomemo M110"));
    REQUIRE_FALSE(name_suggests_ble("Phomemo"));
}

TEST_CASE("name_suggests_ble - Niimbot devices are BLE", "[bluetooth][discovery]") {
    REQUIRE(name_suggests_ble("Niimbot B21"));
    REQUIRE(name_suggests_ble("B21"));
    REQUIRE(name_suggests_ble("B21-A4F3"));
    REQUIRE(name_suggests_ble("D11"));
    REQUIRE(name_suggests_ble("D110"));
    REQUIRE(name_suggests_ble("D110Pro"));
}

TEST_CASE("name_suggests_ble - Supvan/KataSymbol are BLE", "[bluetooth][discovery]") {
    REQUIRE(name_suggests_ble("Supvan E10"));
    REQUIRE(name_suggests_ble("KataSymbol E10"));
    REQUIRE(name_suggests_ble("E10"));
    REQUIRE(name_suggests_ble("E16"));
    REQUIRE(name_suggests_ble("T50M"));
}

TEST_CASE("name_suggests_ble - Brother QL is NOT BLE (Classic SPP)", "[bluetooth][discovery]") {
    REQUIRE_FALSE(name_suggests_ble("QL-820NWB"));
    REQUIRE_FALSE(name_suggests_ble("PT-P710BT"));
    REQUIRE_FALSE(name_suggests_ble("TD-4550DNWB"));
    REQUIRE_FALSE(name_suggests_ble("RJ-4250WB"));
}

TEST_CASE("name_suggests_ble - Dymo/MUNBYN are NOT BLE", "[bluetooth][discovery]") {
    REQUIRE_FALSE(name_suggests_ble("LW-550"));
    REQUIRE_FALSE(name_suggests_ble("DYMO LabelWriter"));
    REQUIRE_FALSE(name_suggests_ble("MUNBYN"));
}

TEST_CASE("name_suggests_ble - generic names are NOT BLE", "[bluetooth][discovery]") {
    REQUIRE_FALSE(name_suggests_ble("My Label Printer"));
    REQUIRE_FALSE(name_suggests_ble("Thermal Printer"));
    REQUIRE_FALSE(name_suggests_ble("Philips Hue"));
}

// ============================================================================
// is_brother_printer
// ============================================================================

using helix::bluetooth::is_brother_printer;

TEST_CASE("is_brother_printer - Brother models", "[bluetooth][discovery]") {
    REQUIRE(is_brother_printer("QL-820NWB"));
    REQUIRE(is_brother_printer("QL-810W"));
    REQUIRE(is_brother_printer("PT-P710BT"));
    REQUIRE(is_brother_printer("TD-4550DNWB"));
    REQUIRE(is_brother_printer("RJ-4250WB"));
}

TEST_CASE("is_brother_pt_printer - PT models", "[bluetooth][discovery]") {
    REQUIRE(is_brother_pt_printer("PT-P300BT"));
    REQUIRE(is_brother_pt_printer("PT-P710BT"));
    REQUIRE(is_brother_pt_printer("pt-p300bt"));
    REQUIRE(is_brother_pt_printer("PT-P900W"));
}

TEST_CASE("is_brother_pt_printer - non-PT models", "[bluetooth][discovery]") {
    REQUIRE_FALSE(is_brother_pt_printer("QL-820NWB"));
    REQUIRE_FALSE(is_brother_pt_printer(""));
    REQUIRE_FALSE(is_brother_pt_printer(nullptr));
    REQUIRE_FALSE(is_brother_pt_printer("Phomemo-M110"));
}

TEST_CASE("is_brother_printer - non-Brother models", "[bluetooth][discovery]") {
    REQUIRE_FALSE(is_brother_printer("M110"));
    REQUIRE_FALSE(is_brother_printer("Phomemo"));
    REQUIRE_FALSE(is_brother_printer("Niimbot B21"));
    REQUIRE_FALSE(is_brother_printer("DYMO LabelWriter"));
    REQUIRE_FALSE(is_brother_printer(nullptr));
    REQUIRE_FALSE(is_brother_printer(""));
}

// ============================================================================
// find_brand
// ============================================================================

using helix::bluetooth::find_brand;

TEST_CASE("find_brand - returns correct brand entries", "[bluetooth][discovery]") {
    auto* b = find_brand("QL-820NWB");
    REQUIRE(b != nullptr);
    REQUIRE(b->is_brother);
    REQUIRE_FALSE(b->is_ble);

    b = find_brand("M110");
    REQUIRE(b != nullptr);
    REQUIRE_FALSE(b->is_brother);
    REQUIRE_FALSE(b->is_ble);

    b = find_brand("B21-A4F3");
    REQUIRE(b != nullptr);
    REQUIRE(b->is_ble);
    REQUIRE_FALSE(b->is_brother);
}

TEST_CASE("find_brand - MakeID and YichipFPGA variants", "[bluetooth][discovery]") {
    auto* b = find_brand("MakeID E1");
    REQUIRE(b != nullptr);
    REQUIRE(b->is_makeid);
    REQUIRE(b->is_ble);
    REQUIRE_FALSE(b->is_brother);

    b = find_brand("MAKEID L1");
    REQUIRE(b != nullptr);
    REQUIRE(b->is_makeid);

    b = find_brand("YichipFPGA-1308");
    REQUIRE(b != nullptr);
    REQUIRE(b->is_makeid);
    REQUIRE(b->is_ble);
}

TEST_CASE("is_likely_label_printer - MakeID and YichipFPGA variants", "[bluetooth][discovery]") {
    REQUIRE(is_likely_label_printer("MakeID E1"));
    REQUIRE(is_likely_label_printer("MAKEID L1"));
    REQUIRE(is_likely_label_printer("YichipFPGA-1308"));
    REQUIRE(is_likely_label_printer("YichipFPGA-2000"));
}

TEST_CASE("name_suggests_ble - MakeID and YichipFPGA are BLE", "[bluetooth][discovery]") {
    REQUIRE(name_suggests_ble("MakeID E1"));
    REQUIRE(name_suggests_ble("MAKEID L1"));
    REQUIRE(name_suggests_ble("YichipFPGA-1308"));
}

TEST_CASE("is_makeid_printer - MakeID and YichipFPGA", "[bluetooth][discovery]") {
    using helix::bluetooth::is_makeid_printer;
    REQUIRE(is_makeid_printer("MakeID E1"));
    REQUIRE(is_makeid_printer("MAKEID L1"));
    REQUIRE(is_makeid_printer("YichipFPGA-1308"));
    REQUIRE_FALSE(is_makeid_printer("QL-820NWB"));
    REQUIRE_FALSE(is_makeid_printer("Niimbot B21"));
    REQUIRE_FALSE(is_makeid_printer(nullptr));
    REQUIRE_FALSE(is_makeid_printer(""));
}

TEST_CASE("find_brand - unknown returns nullptr", "[bluetooth][discovery]") {
    REQUIRE(find_brand(nullptr) == nullptr);
    REQUIRE(find_brand("") == nullptr);
    REQUIRE(find_brand("Samsung Galaxy") == nullptr);
    REQUIRE(find_brand("Philips Hue") == nullptr);
}
