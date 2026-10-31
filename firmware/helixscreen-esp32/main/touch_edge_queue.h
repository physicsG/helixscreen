// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The pointer samples a touch sampler hands to LVGL's indev read, free of
// ESP-IDF so the host suite tests it. Not thread-safe: the caller serialises
// push and pop (touch_input.c holds a spinlock around each call).
//
// The sampler pushes what the controller reports; LVGL pops one sample per
// indev read and keeps reading while more are queued. A frame that takes longer
// than a tap therefore still sees the press, the release and the next press in
// order, instead of only the latest state.
//
// Per tap the queue holds at most three entries: the press, one move (later
// moves overwrite it while it is still queued) and the release. A press is
// accepted only while there is room for all three, so a release always fits:
// a full queue drops a whole tap, never half of one.

#include <stdbool.h>
#include <stdint.h>

#define TOUCH_EDGE_QUEUE_CAP 64

typedef enum {
    TOUCH_EDGE_PRESS,
    TOUCH_EDGE_MOVE,
    TOUCH_EDGE_RELEASE,
} touch_edge_kind;

typedef struct {
    touch_edge_kind kind;
    int16_t x;
    int16_t y;
} touch_edge;

typedef struct {
    touch_edge buf[TOUCH_EDGE_QUEUE_CAP];
    uint8_t head;
    uint8_t count;
    // The state the pushes so far describe, which the queue checks each new
    // sample against.
    bool pushed_pressed;
    // True while a press was refused: its moves and release are refused too.
    bool dropping;
    uint8_t pushed_track;
    int16_t pushed_x;
    int16_t pushed_y;
    // What the last pop reported; an empty queue reports it again.
    touch_edge delivered;
    uint32_t dropped_taps;
} touch_edge_queue;

static inline void touch_edge_queue_init(touch_edge_queue* q) {
    q->head = 0;
    q->count = 0;
    q->pushed_pressed = false;
    q->dropping = false;
    q->pushed_track = 0;
    q->pushed_x = 0;
    q->pushed_y = 0;
    q->delivered.kind = TOUCH_EDGE_RELEASE;
    q->delivered.x = 0;
    q->delivered.y = 0;
    q->dropped_taps = 0;
}

static inline touch_edge* touch_edge_queue_tail_(touch_edge_queue* q) {
    return &q->buf[(q->head + q->count - 1) % TOUCH_EDGE_QUEUE_CAP];
}

static inline void touch_edge_queue_append_(touch_edge_queue* q, touch_edge_kind kind, int16_t x,
                                            int16_t y) {
    touch_edge* e = &q->buf[(q->head + q->count) % TOUCH_EDGE_QUEUE_CAP];
    e->kind = kind;
    e->x = x;
    e->y = y;
    ++q->count;
}

static inline void touch_edge_queue_release_(touch_edge_queue* q) {
    q->pushed_pressed = false;
    if (q->dropping) {
        q->dropping = false;
        return;
    }
    // A release carries the last pressed point: the controller reports none.
    touch_edge_queue_append_(q, TOUCH_EDGE_RELEASE, q->pushed_x, q->pushed_y);
}

// One controller reading: finger `track` pressed at (x, y), or nothing pressed
// (track, x and y ignored). Readings that repeat the current state queue
// nothing. A different track while pressed is a second finger that landed
// before the first lifted (fast typing rolls over keys): the first finger's
// release is queued ahead of the new press, so LVGL sees two taps instead of
// one finger sliding between keys.
static inline void touch_edge_queue_push(touch_edge_queue* q, bool pressed, uint8_t track,
                                         int16_t x, int16_t y) {
    if (pressed && q->pushed_pressed && track != q->pushed_track) {
        touch_edge_queue_release_(q);
    }
    if (pressed && !q->pushed_pressed) {
        q->pushed_pressed = true;
        q->pushed_track = track;
        q->pushed_x = x;
        q->pushed_y = y;
        if (q->count + 3 > TOUCH_EDGE_QUEUE_CAP) {
            q->dropping = true;
            ++q->dropped_taps;
            return;
        }
        touch_edge_queue_append_(q, TOUCH_EDGE_PRESS, x, y);
        return;
    }
    if (!pressed) {
        if (q->pushed_pressed) {
            touch_edge_queue_release_(q);
        }
        return;
    }
    if (q->dropping || (x == q->pushed_x && y == q->pushed_y)) {
        return;
    }
    q->pushed_x = x;
    q->pushed_y = y;
    if (q->count > 0 && touch_edge_queue_tail_(q)->kind == TOUCH_EDGE_MOVE) {
        touch_edge_queue_tail_(q)->x = x;
        touch_edge_queue_tail_(q)->y = y;
    } else if (q->count + 2 <= TOUCH_EDGE_QUEUE_CAP) {
        // Room for this move and the release still to come.
        touch_edge_queue_append_(q, TOUCH_EDGE_MOVE, x, y);
    }
}

// The next sample for LVGL. Returns whether more are queued behind it, which
// is what the indev read reports as continue_reading. An empty queue repeats
// the last sample delivered, which is the state LVGL already holds.
static inline bool touch_edge_queue_pop(touch_edge_queue* q, touch_edge* out) {
    if (q->count > 0) {
        q->delivered = q->buf[q->head];
        q->head = (uint8_t)((q->head + 1) % TOUCH_EDGE_QUEUE_CAP);
        --q->count;
    }
    *out = q->delivered;
    return q->count > 0;
}

static inline bool touch_edge_is_pressed(const touch_edge* e) {
    return e->kind != TOUCH_EDGE_RELEASE;
}
