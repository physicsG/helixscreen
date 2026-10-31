// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// How the touch sampler reacts to controller reads that fail, free of ESP-IDF
// so the host suite tests it. A degraded ribbon fails every read; at the
// normal poll rate that would be a hundred log lines a second from a task that
// outranks the UI. So failures are logged at most once per interval, with a
// count, and after a run of them the poll slows until a read succeeds.

#include <stdbool.h>
#include <stdint.h>

#define TOUCH_READ_BACKOFF_AFTER 10
#define TOUCH_READ_BACKOFF_PERIOD_MS 100
#define TOUCH_READ_LOG_INTERVAL_MS 10000

typedef struct {
    uint32_t consecutive_failures;
    uint32_t unlogged_failures;
    int64_t last_log_ms;
    bool logged;
} touch_read_health;

typedef struct {
    uint32_t next_period_ms; // how long the sampler sleeps before its next read
    bool log;                // log now, reporting `failures`
    uint32_t failures;       // failures since the last log, this one included
} touch_read_verdict;

static inline void touch_read_health_init(touch_read_health* h) {
    h->consecutive_failures = 0;
    h->unlogged_failures = 0;
    h->last_log_ms = 0;
    h->logged = false;
}

static inline touch_read_verdict
touch_read_health_record(touch_read_health* h, bool ok, int64_t now_ms, uint32_t normal_period_ms) {
    touch_read_verdict v = {normal_period_ms, false, 0};
    if (ok) {
        h->consecutive_failures = 0;
        return v;
    }
    ++h->consecutive_failures;
    ++h->unlogged_failures;
    if (h->consecutive_failures >= TOUCH_READ_BACKOFF_AFTER) {
        v.next_period_ms = TOUCH_READ_BACKOFF_PERIOD_MS;
    }
    if (!h->logged || now_ms - h->last_log_ms >= TOUCH_READ_LOG_INTERVAL_MS) {
        v.log = true;
        v.failures = h->unlogged_failures;
        h->unlogged_failures = 0;
        h->last_log_ms = now_ms;
        h->logged = true;
    }
    return v;
}
