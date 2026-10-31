// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
//
// TEST_MIRROR_OK: exercises the shipped ESP32 header unmodified --
//                 firmware/helixscreen-esp32/main/touch_read_health.h. That
//                 is production firmware code; this gate only scans include/ and src/, so
//                 a firmware/ include reads to it as no include at all.

/**
 * @file test_esp32_touch_read_health.cpp
 * @brief The K-Touch touch sampler's response to failing controller reads: bounded logging
 *        and a slower poll while the controller does not answer.
 */

#include "firmware/helixscreen-esp32/main/touch_read_health.h"

#include "../catch_amalgamated.hpp"

namespace {
constexpr uint32_t kNormalMs = 10;
}

TEST_CASE("Touch read health: a dead controller logs once per interval with a count",
          "[esp32][touch_health]") {
    touch_read_health h;
    touch_read_health_init(&h);

    // Ten seconds of failures at 10 ms: the first one logs at once, the rest wait.
    int logs = 0;
    int64_t now = 1000;
    touch_read_verdict v = touch_read_health_record(&h, false, now, kNormalMs);
    CHECK(v.log);
    CHECK(v.failures == 1);
    for (int i = 0; i < 999; ++i) {
        now += kNormalMs;
        v = touch_read_health_record(&h, false, now, kNormalMs);
        logs += v.log ? 1 : 0;
    }
    CHECK(logs == 0);

    // The interval has passed: the next failure reports everything since the last log.
    now = 1000 + TOUCH_READ_LOG_INTERVAL_MS;
    v = touch_read_health_record(&h, false, now, kNormalMs);
    CHECK(v.log);
    CHECK(v.failures == 1000);
}

TEST_CASE("Touch read health: a run of failures slows the poll until a read succeeds",
          "[esp32][touch_health]") {
    touch_read_health h;
    touch_read_health_init(&h);

    touch_read_verdict v{};
    for (int i = 1; i < TOUCH_READ_BACKOFF_AFTER; ++i) {
        v = touch_read_health_record(&h, false, i, kNormalMs);
        CHECK(v.next_period_ms == kNormalMs);
    }
    v = touch_read_health_record(&h, false, 100, kNormalMs);
    CHECK(v.next_period_ms == TOUCH_READ_BACKOFF_PERIOD_MS);

    v = touch_read_health_record(&h, true, 200, kNormalMs);
    CHECK(v.next_period_ms == kNormalMs);
    CHECK_FALSE(v.log);

    // One failure after a success is not a run.
    v = touch_read_health_record(&h, false, 210, kNormalMs);
    CHECK(v.next_period_ms == kNormalMs);
}
