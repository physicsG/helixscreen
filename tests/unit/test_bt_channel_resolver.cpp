// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../lvgl_test_fixture.h"
#include "bluetooth_loader.h"
#include "bt_print_utils.h"
#include "label_printer_settings.h"

#include <cerrno>

#include "../catch_amalgamated.hpp"

namespace {

// Scoped swap of the global loader's sdp_find_rfcomm_channel pointer.
struct LoaderMock {
    helix_bt_sdp_find_rfcomm_channel_fn saved_sdp;
    LoaderMock() {
        auto& l = helix::bluetooth::BluetoothLoader::instance();
        saved_sdp = l.sdp_find_rfcomm_channel;
    }
    ~LoaderMock() {
        auto& l = helix::bluetooth::BluetoothLoader::instance();
        l.sdp_find_rfcomm_channel = saved_sdp;
    }
};

// Script + counters for the fake SDP function.
static int g_sdp_calls = 0;
static int g_sdp_next_result = 0; // 0 = success, negative = failure
static int g_sdp_next_channel = 5;

extern "C" int fake_sdp(helix_bt_context*, const char*, uint16_t, int* out) {
    g_sdp_calls++;
    if (g_sdp_next_result == 0) {
        *out = g_sdp_next_channel;
        return 0;
    }
    return g_sdp_next_result;
}

void reset_fake() {
    g_sdp_calls = 0;
    g_sdp_next_result = 0;
    g_sdp_next_channel = 5;
}

// RAII: ensure the bt_channel cache is zeroed even if a REQUIRE throws mid-test,
// so leftover state cannot leak into subsequent tests.
struct SettingsChannelResetter {
    SettingsChannelResetter() {
        helix::LabelPrinterSettingsManager::instance().init_subjects();
        helix::LabelPrinterSettingsManager::instance().set_bt_channel(0);
        reset_fake();
    }
    ~SettingsChannelResetter() {
        helix::LabelPrinterSettingsManager::instance().set_bt_channel(0);
        reset_fake();
    }
};

} // namespace

TEST_CASE("resolve_label_printer_channel: empty cache runs SDP and caches", "[bt][resolver]") {
    LVGLTestFixture fixture;
    LoaderMock mock;
    SettingsChannelResetter reset;
    auto& loader = helix::bluetooth::BluetoothLoader::instance();
    loader.sdp_find_rfcomm_channel = &fake_sdp;
    auto& s = helix::LabelPrinterSettingsManager::instance();
    g_sdp_next_channel = 7;

    int ch = helix::label::resolve_label_printer_channel("AA:BB:CC:DD:EE:FF", 1);
    REQUIRE(ch == 7);
    REQUIRE(g_sdp_calls == 1);
    REQUIRE(s.get_bt_channel() == 7);
}

TEST_CASE("resolve_label_printer_channel: populated cache skips SDP", "[bt][resolver]") {
    LVGLTestFixture fixture;
    LoaderMock mock;
    SettingsChannelResetter reset;
    auto& loader = helix::bluetooth::BluetoothLoader::instance();
    loader.sdp_find_rfcomm_channel = &fake_sdp;
    helix::LabelPrinterSettingsManager::instance().set_bt_channel(9);
    reset_fake(); // re-zero counters after the seeded set

    int ch = helix::label::resolve_label_printer_channel("AA:BB:CC:DD:EE:FF", 1);
    REQUIRE(ch == 9);
    REQUIRE(g_sdp_calls == 0);
}

TEST_CASE("resolve_label_printer_channel: SDP failure returns fallback, does not cache",
          "[bt][resolver]") {
    LVGLTestFixture fixture;
    LoaderMock mock;
    SettingsChannelResetter reset;
    auto& loader = helix::bluetooth::BluetoothLoader::instance();
    loader.sdp_find_rfcomm_channel = &fake_sdp;
    g_sdp_next_result = -ENOENT;

    int ch = helix::label::resolve_label_printer_channel("AA:BB:CC:DD:EE:FF", 3);
    REQUIRE(ch == 3);
    REQUIRE(helix::LabelPrinterSettingsManager::instance().get_bt_channel() == 0);
}

TEST_CASE("resolve_label_printer_channel: SDP failure with no fallback returns -1",
          "[bt][resolver]") {
    LVGLTestFixture fixture;
    LoaderMock mock;
    SettingsChannelResetter reset;
    auto& loader = helix::bluetooth::BluetoothLoader::instance();
    loader.sdp_find_rfcomm_channel = &fake_sdp;
    g_sdp_next_result = -ENOENT;

    int ch = helix::label::resolve_label_printer_channel("AA:BB:CC:DD:EE:FF", 0);
    REQUIRE(ch == -1);
}

TEST_CASE("resolve_label_printer_channel: out-of-range SDP result treated as failure",
          "[bt][resolver]") {
    LVGLTestFixture fixture;
    LoaderMock mock;
    SettingsChannelResetter reset;
    auto& loader = helix::bluetooth::BluetoothLoader::instance();
    loader.sdp_find_rfcomm_channel = &fake_sdp;
    g_sdp_next_channel = 99; // invalid

    int ch = helix::label::resolve_label_printer_channel("AA:BB:CC:DD:EE:FF", 1);
    REQUIRE(ch == 1);
    REQUIRE(helix::LabelPrinterSettingsManager::instance().get_bt_channel() == 0);
}

TEST_CASE("resolve_label_printer_channel: null SDP function pointer falls back", "[bt][resolver]") {
    LVGLTestFixture fixture;
    LoaderMock mock;
    SettingsChannelResetter reset;
    // Simulate plugin missing or symbol unresolved.
    helix::bluetooth::BluetoothLoader::instance().sdp_find_rfcomm_channel = nullptr;

    int ch = helix::label::resolve_label_printer_channel("AA:BB:CC:DD:EE:FF", 4);
    REQUIRE(ch == 4);
    REQUIRE(g_sdp_calls == 0);
    REQUIRE(helix::LabelPrinterSettingsManager::instance().get_bt_channel() == 0);
}

TEST_CASE("resolve_label_printer_channel: null SDP with invalid fallback returns -1",
          "[bt][resolver]") {
    LVGLTestFixture fixture;
    LoaderMock mock;
    SettingsChannelResetter reset;
    helix::bluetooth::BluetoothLoader::instance().sdp_find_rfcomm_channel = nullptr;

    int ch = helix::label::resolve_label_printer_channel("AA:BB:CC:DD:EE:FF", 0);
    REQUIRE(ch == -1);
}
