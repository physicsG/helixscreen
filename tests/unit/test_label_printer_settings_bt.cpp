// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../lvgl_test_fixture.h"
#include "label_printer_settings.h"

#include "../catch_amalgamated.hpp"

using namespace helix;

TEST_CASE("Label printer settings - bluetooth type", "[label][settings]") {
    LVGLTestFixture fixture;

    auto& settings = LabelPrinterSettingsManager::instance();
    settings.init_subjects();

    settings.set_printer_type("bluetooth");
    REQUIRE(settings.get_printer_type() == "bluetooth");
}

TEST_CASE("Label printer settings - bt_address persistence", "[label][settings]") {
    LVGLTestFixture fixture;

    auto& settings = LabelPrinterSettingsManager::instance();
    settings.init_subjects();

    settings.set_bt_address("AA:BB:CC:DD:EE:FF");
    REQUIRE(settings.get_bt_address() == "AA:BB:CC:DD:EE:FF");
}

TEST_CASE("Label printer settings - bt_transport persistence", "[label][settings]") {
    LVGLTestFixture fixture;

    auto& settings = LabelPrinterSettingsManager::instance();
    settings.init_subjects();

    settings.set_bt_transport("spp");
    REQUIRE(settings.get_bt_transport() == "spp");

    settings.set_bt_transport("ble");
    REQUIRE(settings.get_bt_transport() == "ble");
}

TEST_CASE("Label printer settings - BT configured check", "[label][settings]") {
    LVGLTestFixture fixture;

    auto& settings = LabelPrinterSettingsManager::instance();
    settings.init_subjects();

    settings.set_printer_type("bluetooth");

    settings.set_bt_address("");
    REQUIRE_FALSE(settings.is_configured());

    settings.set_bt_address("AA:BB:CC:DD:EE:FF");
    REQUIRE(settings.is_configured());
}

TEST_CASE("LabelPrinterSettings bt_channel round-trips", "[label_printer][bt]") {
    LVGLTestFixture fixture;

    auto& s = LabelPrinterSettingsManager::instance();
    s.init_subjects();

    s.set_bt_channel(0);
    REQUIRE(s.get_bt_channel() == 0);

    s.set_bt_channel(7);
    REQUIRE(s.get_bt_channel() == 7);

    s.set_bt_channel(0); // reset for subsequent tests
}
