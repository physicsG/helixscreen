// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "wifi_ap_steering.h"

#include "../catch_amalgamated.hpp"

using helix::WifiApSteering;
using Bssid = WifiApSteering::Bssid;

namespace {

const Bssid kApA{0x76, 0x83, 0xc2, 0x9c, 0xac, 0x2a};
const Bssid kApB{0x7e, 0x45, 0x58, 0xf7, 0xae, 0x34};
const Bssid kApC{0x7e, 0x45, 0x58, 0xf7, 0xa2, 0x7c};

constexpr int64_t STALL = WifiApSteering::kLinkStallSilenceMs;

// Feeds the drops that make the steering want to leave the current access point.
void stall_out(WifiApSteering& s, int64_t start_ms) {
    for (int i = 0; i < WifiApSteering::kLinkStallDrops - 1; ++i) {
        REQUIRE_FALSE(s.on_link_drop(start_ms + i * 30'000, STALL));
    }
    REQUIRE(s.on_link_drop(start_ms + (WifiApSteering::kLinkStallDrops - 1) * 30'000, STALL));
}

} // namespace

TEST_CASE("AP steering: repeated stalled drops on one AP ask to leave it", "[wifi][steering]") {
    WifiApSteering s;
    s.on_associated(kApA);
    stall_out(s, 0);
}

TEST_CASE("AP steering: a drop after recent traffic does not count", "[wifi][steering]") {
    WifiApSteering s;
    s.on_associated(kApA);
    for (int i = 0; i < 10; ++i) {
        // Moonraker or Klipper restarting: frames were arriving until the drop.
        REQUIRE_FALSE(s.on_link_drop(i * 30'000, STALL - 1));
    }
    CHECK(s.stalled_drops() == 0);
}

TEST_CASE("AP steering: no drop counts before an association", "[wifi][steering]") {
    WifiApSteering s;
    for (int i = 0; i < 10; ++i) {
        REQUIRE_FALSE(s.on_link_drop(i * 30'000, STALL));
    }
}

TEST_CASE("AP steering: drops spread wider than the window start the count again",
          "[wifi][steering]") {
    WifiApSteering s;
    s.on_associated(kApA);
    REQUIRE_FALSE(s.on_link_drop(0, STALL));
    REQUIRE_FALSE(s.on_link_drop(1'000, STALL));
    // The third drop lands after the window that opened with the first.
    REQUIRE_FALSE(s.on_link_drop(WifiApSteering::kLinkStallWindowMs + 1, STALL));
    CHECK(s.stalled_drops() == 1);
}

TEST_CASE("AP steering: joining a different AP starts the count again", "[wifi][steering]") {
    WifiApSteering s;
    s.on_associated(kApA);
    REQUIRE_FALSE(s.on_link_drop(0, STALL));
    REQUIRE_FALSE(s.on_link_drop(1'000, STALL));
    s.on_associated(kApB);
    CHECK(s.stalled_drops() == 0);
    REQUIRE_FALSE(s.on_link_drop(2'000, STALL));

    // Reassociating to the same AP keeps its record.
    s.on_associated(kApB);
    CHECK(s.stalled_drops() == 1);
}

TEST_CASE("AP steering: picks the strongest other AP of the SSID", "[wifi][steering]") {
    WifiApSteering s;
    s.on_associated(kApA);
    stall_out(s, 0);
    auto pick = s.pick_alternative({{kApA, -60}, {kApB, -74}, {kApC, -70}}, 100'000);
    REQUIRE(pick.has_value());
    CHECK(*pick == kApC);
    CHECK(s.stalled_drops() == 0);
}

TEST_CASE("AP steering: a single-AP network stays put", "[wifi][steering]") {
    WifiApSteering s;
    s.on_associated(kApA);
    stall_out(s, 0);
    CHECK_FALSE(s.pick_alternative({{kApA, -70}}, 100'000).has_value());
    CHECK_FALSE(s.pick_alternative({}, 100'000).has_value());
    // The count starts over, so the next scan waits for another full run of drops.
    CHECK(s.stalled_drops() == 0);
}

TEST_CASE("AP steering: an alternative too weak to use is not picked", "[wifi][steering]") {
    WifiApSteering s;
    s.on_associated(kApA);
    stall_out(s, 0);
    CHECK_FALSE(s.pick_alternative({{kApB, WifiApSteering::kMinRssi - 1}}, 100'000).has_value());
    auto pick = s.pick_alternative({{kApB, WifiApSteering::kMinRssi}}, 100'000);
    REQUIRE(pick.has_value());
    CHECK(*pick == kApB);
}

TEST_CASE("AP steering: an abandoned AP is skipped, then used again once it recovers",
          "[wifi][steering]") {
    WifiApSteering s;
    s.on_associated(kApA);
    stall_out(s, 0);
    REQUIRE(s.pick_alternative({{kApA, -70}, {kApB, -74}}, 100'000) == kApB);

    // B stalls too while A is still avoided: nowhere better to go.
    s.on_associated(kApB);
    stall_out(s, 200'000);
    CHECK_FALSE(s.pick_alternative({{kApA, -70}, {kApB, -74}}, 300'000).has_value());

    // Long after A was abandoned it is eligible again.
    stall_out(s, 300'000 + WifiApSteering::kAvoidMs);
    auto pick = s.pick_alternative({{kApA, -70}, {kApB, -74}}, 400'000 + WifiApSteering::kAvoidMs);
    REQUIRE(pick.has_value());
    CHECK(*pick == kApA);
}

TEST_CASE("AP steering: an alternative much weaker than the current AP is not picked",
          "[wifi][steering]") {
    // Stalls caused by the printer side (a reboot, a busy Moonraker) count too,
    // so they must not be able to move a strong link to a marginal one.
    WifiApSteering s;
    s.on_associated(kApA);
    stall_out(s, 0);
    const int margin = WifiApSteering::kMaxRssiLossDb;
    CHECK_FALSE(s.pick_alternative({{kApA, -45}, {kApB, -45 - margin - 1}}, 100'000).has_value());

    stall_out(s, 200'000);
    auto pick = s.pick_alternative({{kApA, -45}, {kApB, -45 - margin}}, 300'000);
    REQUIRE(pick.has_value());
    CHECK(*pick == kApB);
}

TEST_CASE("AP steering: only a scan started for steering, and still recent, answers it",
          "[wifi][steering]") {
    WifiApSteering s;
    s.on_associated(kApA);
    // No request: an unrelated scan (the WiFi settings list) steers nothing.
    CHECK_FALSE(s.take_scan_request(0));

    stall_out(s, 0);
    s.scan_started(100'000);
    CHECK(s.take_scan_request(103'000));
    // Answered once.
    CHECK_FALSE(s.take_scan_request(104'000));

    // A request whose scan never completed expires.
    s.scan_started(200'000);
    CHECK_FALSE(s.take_scan_request(200'000 + WifiApSteering::kScanRequestTtlMs + 1));
    CHECK_FALSE(s.take_scan_request(200'000 + WifiApSteering::kScanRequestTtlMs + 2));
}

TEST_CASE("AP steering: formats a BSSID for the log", "[wifi][steering]") {
    CHECK(helix::format_bssid(kApA) == "76:83:c2:9c:ac:2a");
}
