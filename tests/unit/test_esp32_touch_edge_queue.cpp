// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
//
// TEST_MIRROR_OK: exercises the shipped ESP32 header unmodified --
//                 firmware/helixscreen-esp32/main/touch_edge_queue.h. That
//                 is production firmware code; this gate only scans include/ and src/, so
//                 a firmware/ include reads to it as no include at all.

/**
 * @file test_esp32_touch_edge_queue.cpp
 * @brief The K-Touch touch sampler's queue: every press and release reaches LVGL in order,
 *        however many arrive between two indev reads (prestonbrown/helixscreen#1884).
 */

#include "firmware/helixscreen-esp32/main/touch_edge_queue.h"

#include <vector>

#include "../catch_amalgamated.hpp"

namespace {

struct Read {
    touch_edge edge;
    bool more;
};

// One LVGL indev read: pops until the queue says nothing more is queued, as
// lv_indev's continue_reading loop does.
std::vector<Read> drain(touch_edge_queue& q) {
    std::vector<Read> reads;
    for (;;) {
        Read r{};
        r.more = touch_edge_queue_pop(&q, &r.edge);
        reads.push_back(r);
        if (!r.more) {
            return reads;
        }
    }
}

} // namespace

TEST_CASE("Touch queue: two taps between reads arrive as four samples in order",
          "[esp32][touch_queue]") {
    touch_edge_queue q;
    touch_edge_queue_init(&q);

    touch_edge_queue_push(&q, true, 0, 100, 200);
    touch_edge_queue_push(&q, false, 0, 0, 0);
    touch_edge_queue_push(&q, true, 0, 300, 210);
    touch_edge_queue_push(&q, false, 0, 0, 0);

    const std::vector<Read> reads = drain(q);
    REQUIRE(reads.size() == 4);
    CHECK(reads[0].edge.kind == TOUCH_EDGE_PRESS);
    CHECK(reads[0].edge.x == 100);
    CHECK(reads[0].more);
    CHECK(reads[1].edge.kind == TOUCH_EDGE_RELEASE);
    CHECK(reads[1].edge.x == 100);
    CHECK(reads[1].more);
    CHECK(reads[2].edge.kind == TOUCH_EDGE_PRESS);
    CHECK(reads[2].edge.x == 300);
    CHECK(reads[2].edge.y == 210);
    CHECK(reads[2].more);
    CHECK(reads[3].edge.kind == TOUCH_EDGE_RELEASE);
    CHECK(reads[3].edge.x == 300);
    CHECK_FALSE(reads[3].more);
}

TEST_CASE("Touch queue: an empty queue repeats the state LVGL already holds",
          "[esp32][touch_queue]") {
    touch_edge_queue q;
    touch_edge_queue_init(&q);
    touch_edge e{};

    CHECK_FALSE(touch_edge_queue_pop(&q, &e));
    CHECK_FALSE(touch_edge_is_pressed(&e));

    touch_edge_queue_push(&q, true, 0, 40, 50);
    CHECK_FALSE(touch_edge_queue_pop(&q, &e));
    CHECK(touch_edge_is_pressed(&e));
    // Held still: nothing new is queued and the press is reported again.
    touch_edge_queue_push(&q, true, 0, 40, 50);
    CHECK_FALSE(touch_edge_queue_pop(&q, &e));
    CHECK(e.kind == TOUCH_EDGE_PRESS);
    CHECK(e.x == 40);
}

TEST_CASE("Touch queue: moves between reads collapse to the latest point", "[esp32][touch_queue]") {
    touch_edge_queue q;
    touch_edge_queue_init(&q);

    touch_edge_queue_push(&q, true, 0, 10, 10);
    for (int16_t y = 11; y <= 60; ++y) {
        touch_edge_queue_push(&q, true, 0, 10, y);
    }
    touch_edge_queue_push(&q, false, 0, 0, 0);

    const std::vector<Read> reads = drain(q);
    REQUIRE(reads.size() == 3);
    CHECK(reads[0].edge.kind == TOUCH_EDGE_PRESS);
    CHECK(reads[0].edge.y == 10);
    CHECK(reads[1].edge.kind == TOUCH_EDGE_MOVE);
    CHECK(reads[1].edge.y == 60);
    CHECK(reads[2].edge.kind == TOUCH_EDGE_RELEASE);
    CHECK(reads[2].edge.y == 60);
}

TEST_CASE("Touch queue: a move after its press was read is queued on its own",
          "[esp32][touch_queue]") {
    touch_edge_queue q;
    touch_edge_queue_init(&q);
    touch_edge e{};

    touch_edge_queue_push(&q, true, 0, 10, 10);
    touch_edge_queue_pop(&q, &e);
    touch_edge_queue_push(&q, true, 0, 10, 30);

    CHECK_FALSE(touch_edge_queue_pop(&q, &e));
    CHECK(e.kind == TOUCH_EDGE_MOVE);
    CHECK(e.y == 30);
}

TEST_CASE("Touch queue: a full queue drops whole taps, never a lone release",
          "[esp32][touch_queue]") {
    touch_edge_queue q;
    touch_edge_queue_init(&q);

    // Far more taps than fit, with a move in each, and no reads in between.
    constexpr int kTaps = TOUCH_EDGE_QUEUE_CAP;
    for (int16_t i = 0; i < kTaps; ++i) {
        touch_edge_queue_push(&q, true, 0, i, 0);
        touch_edge_queue_push(&q, true, 0, i, 5);
        touch_edge_queue_push(&q, false, 0, 0, 0);
    }
    CHECK(q.dropped_taps > 0);

    const std::vector<Read> reads = drain(q);
    bool pressed = false;
    int presses = 0;
    for (const Read& r : reads) {
        if (r.edge.kind == TOUCH_EDGE_PRESS) {
            CHECK_FALSE(pressed);
            pressed = true;
            ++presses;
        } else if (r.edge.kind == TOUCH_EDGE_MOVE) {
            CHECK(pressed);
        } else {
            CHECK(pressed);
            pressed = false;
        }
    }
    CHECK_FALSE(pressed);
    CHECK(presses + static_cast<int>(q.dropped_taps) == kTaps);

    // Room again: the next tap gets through whole.
    touch_edge_queue_push(&q, true, 0, 99, 99);
    touch_edge_queue_push(&q, false, 0, 0, 0);
    const std::vector<Read> after = drain(q);
    REQUIRE(after.size() == 2);
    CHECK(after[0].edge.kind == TOUCH_EDGE_PRESS);
    CHECK(after[1].edge.kind == TOUCH_EDGE_RELEASE);
}

TEST_CASE("Touch queue: a press held while the queue is full stays dropped until released",
          "[esp32][touch_queue]") {
    touch_edge_queue q;
    touch_edge_queue_init(&q);

    while (q.count + 3 <= TOUCH_EDGE_QUEUE_CAP) {
        touch_edge_queue_push(&q, true, 0, 1, 1);
        touch_edge_queue_push(&q, false, 0, 0, 0);
    }
    const uint8_t full = q.count;
    touch_edge_queue_push(&q, true, 0, 7, 7);
    touch_edge_queue_push(&q, true, 0, 8, 8);
    touch_edge_queue_push(&q, false, 0, 0, 0);
    CHECK(q.count == full);
    CHECK(q.dropped_taps == 1);
}

TEST_CASE("Touch queue: a second finger before the first lifts is a release, then a press",
          "[esp32][touch_queue]") {
    touch_edge_queue q;
    touch_edge_queue_init(&q);

    // Rollover: the controller's first point switches from finger 0 on one key
    // to finger 1 on the next, with no reading in which nothing is pressed.
    touch_edge_queue_push(&q, true, 0, 100, 200);
    touch_edge_queue_push(&q, true, 1, 300, 200);
    touch_edge_queue_push(&q, false, 0, 0, 0);

    const std::vector<Read> reads = drain(q);
    REQUIRE(reads.size() == 4);
    CHECK(reads[0].edge.kind == TOUCH_EDGE_PRESS);
    CHECK(reads[0].edge.x == 100);
    CHECK(reads[1].edge.kind == TOUCH_EDGE_RELEASE);
    CHECK(reads[1].edge.x == 100);
    CHECK(reads[2].edge.kind == TOUCH_EDGE_PRESS);
    CHECK(reads[2].edge.x == 300);
    CHECK(reads[3].edge.kind == TOUCH_EDGE_RELEASE);
    CHECK(reads[3].edge.x == 300);
}
