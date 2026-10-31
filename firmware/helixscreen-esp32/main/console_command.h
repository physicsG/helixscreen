// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The serial console's command grammar, free of ESP-IDF so the host suite tests it.
// It runs on the console reader task, whose stack is small, so numbers go through
// strtol rather than newlib's much deeper sscanf.

#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    CONSOLE_CMD_NONE,
    CONSOLE_CMD_SNAP,
    CONSOLE_CMD_NOTES,
    CONSOLE_CMD_STACK,
    CONSOLE_CMD_SNAPLINE, // a = line number
    CONSOLE_CMD_TAP,      // a = x, b = y, panel coordinates
    CONSOLE_CMD_DOWN,     // a = x, b = y: a new finger lands, the previous one (if any) lifts
    CONSOLE_CMD_UP,       // every finger lifts
} console_cmd_kind;

typedef struct {
    console_cmd_kind kind;
    int a;
    int b;
} console_cmd;

// Tap coordinates are carried as int16_t into the touch driver.
#define CONSOLE_TAP_COORD_MAX 32767L
#define CONSOLE_SNAPLINE_MAX 2147483647L

// Reads one or more spaces then a non-negative decimal no larger than max from *p,
// and advances *p past it. No sign, no other leading characters.
static inline bool console_parse_number(const char** p, long max, int* out) {
    const char* s = *p;
    if (*s != ' ') {
        return false;
    }
    while (*s == ' ') {
        ++s;
    }
    if (*s < '0' || *s > '9') {
        return false;
    }
    char* end = NULL;
    errno = 0;
    const long v = strtol(s, &end, 10);
    if (errno == ERANGE || v > max) {
        return false;
    }
    *out = (int)v;
    *p = end;
    return true;
}

// A command with trailing characters, a missing or out-of-range number, or an
// unknown word is CONSOLE_CMD_NONE.
static inline console_cmd console_parse_line(const char* line) {
    console_cmd cmd = {CONSOLE_CMD_NONE, 0, 0};
    if (strcmp(line, "snap") == 0) {
        cmd.kind = CONSOLE_CMD_SNAP;
    } else if (strcmp(line, "notes") == 0) {
        cmd.kind = CONSOLE_CMD_NOTES;
    } else if (strcmp(line, "stack") == 0) {
        cmd.kind = CONSOLE_CMD_STACK;
    } else if (strcmp(line, "up") == 0) {
        cmd.kind = CONSOLE_CMD_UP;
    } else if (strncmp(line, "snapline", 8) == 0) {
        const char* p = line + 8;
        if (console_parse_number(&p, CONSOLE_SNAPLINE_MAX, &cmd.a) && *p == '\0') {
            cmd.kind = CONSOLE_CMD_SNAPLINE;
        }
    } else if (strncmp(line, "tap", 3) == 0) {
        const char* p = line + 3;
        if (console_parse_number(&p, CONSOLE_TAP_COORD_MAX, &cmd.a) &&
            console_parse_number(&p, CONSOLE_TAP_COORD_MAX, &cmd.b) && *p == '\0') {
            cmd.kind = CONSOLE_CMD_TAP;
        }
    } else if (strncmp(line, "down", 4) == 0) {
        const char* p = line + 4;
        if (console_parse_number(&p, CONSOLE_TAP_COORD_MAX, &cmd.a) &&
            console_parse_number(&p, CONSOLE_TAP_COORD_MAX, &cmd.b) && *p == '\0') {
            cmd.kind = CONSOLE_CMD_DOWN;
        }
    }
    return cmd;
}
