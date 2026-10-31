// Copyright (C) 2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../../include/wifi_control_breaker.h"

#include "../catch_amalgamated.hpp"

using helix::wifi::ControlBreaker;
using Change = ControlBreaker::Change;

// ControlBreaker is a pure state machine with caller-supplied time, so these
// exercise it directly with no fixture and no sleeping.

namespace {
constexpr uint32_t kFast = 20;
constexpr uint32_t kSlow = 5000;
} // namespace

TEST_CASE("ControlBreaker trips on the third consecutive slow call, not before",
          "[wifi][wifi_breaker]") {
    ControlBreaker b;
    REQUIRE(b.config().trip_after == 3);
    REQUIRE(b.config().slow_call_ms == kSlow);

    CHECK(b.record(kSlow, 0) == Change::None);
    CHECK(b.record(kSlow, 0) == Change::None);
    CHECK_FALSE(b.open());
    CHECK(b.allow_call(0));
    CHECK(b.record(kSlow, 0) == Change::Tripped);
    CHECK(b.open());
}

TEST_CASE("ControlBreaker: a call under the threshold resets the strike count",
          "[wifi][wifi_breaker]") {
    ControlBreaker b;
    b.record(kSlow, 0);
    b.record(kSlow, 0);
    CHECK(b.record(kSlow - 1, 0) == Change::None);
    b.record(kSlow, 0);
    b.record(kSlow, 0);
    CHECK_FALSE(b.open());
}

TEST_CASE("ControlBreaker admits one probe per backoff window and doubles it to the cap",
          "[wifi][wifi_breaker]") {
    ControlBreaker b;
    for (int i = 0; i < 3; ++i) {
        b.record(kSlow, 1000);
    }
    REQUIRE(b.open());

    // Nothing before the first window ends.
    CHECK_FALSE(b.allow_call(1000));
    CHECK_FALSE(b.allow_call(1000 + 15000 - 1));

    // The window's one probe, and no second while it is out.
    CHECK(b.allow_call(1000 + 15000));
    CHECK_FALSE(b.allow_call(1000 + 15000));

    // The probe is slow: the next window is twice as long.
    uint64_t now = 16000 + kSlow;
    b.record(kSlow, now);
    CHECK_FALSE(b.allow_call(now + 30000 - 1));
    CHECK(b.allow_call(now + 30000));

    // Keep failing until the cap holds.
    for (int i = 0; i < 6; ++i) {
        now += 200000;
        b.record(kSlow, now);
    }
    CHECK_FALSE(b.allow_call(now + 120000 - 1));
    CHECK(b.allow_call(now + 120000));
}

TEST_CASE("ControlBreaker recovers on the first call that answers in time",
          "[wifi][wifi_breaker]") {
    ControlBreaker b;
    for (int i = 0; i < 3; ++i) {
        b.record(kSlow, 0);
    }
    REQUIRE(b.open());
    REQUIRE(b.allow_call(15000));

    CHECK(b.record(kFast, 15000) == Change::Recovered);
    CHECK_FALSE(b.open());
    CHECK(b.allow_call(15000));

    // Recovery starts the count over: two more slow calls do not re-trip.
    b.record(kSlow, 16000);
    b.record(kSlow, 16000);
    CHECK_FALSE(b.open());
}

TEST_CASE("ControlBreaker honours an injected configuration", "[wifi][wifi_breaker]") {
    ControlBreaker b(ControlBreaker::Config{50, 2, 100, 400});
    b.record(49, 0);
    b.record(50, 0);
    CHECK_FALSE(b.open());
    CHECK(b.record(50, 0) == Change::Tripped);
    CHECK_FALSE(b.allow_call(99));
    CHECK(b.allow_call(100));
}
