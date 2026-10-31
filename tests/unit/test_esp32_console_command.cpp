// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
//
// TEST_MIRROR_OK: exercises the shipped ESP32 header unmodified --
//                 firmware/helixscreen-esp32/main/console_command.h. That
//                 is production firmware code; this gate only scans include/ and src/, so
//                 a firmware/ include reads to it as no include at all.

/**
 * @file test_esp32_console_command.cpp
 * @brief The K-Touch serial console's command grammar (snap, notes, stack, snapline, tap).
 */

#include "firmware/helixscreen-esp32/main/console_command.h"

#include "../catch_amalgamated.hpp"

TEST_CASE("Console command: bare words", "[esp32][console]") {
    CHECK(console_parse_line("snap").kind == CONSOLE_CMD_SNAP);
    CHECK(console_parse_line("notes").kind == CONSOLE_CMD_NOTES);
    CHECK(console_parse_line("stack").kind == CONSOLE_CMD_STACK);
    CHECK(console_parse_line("snap ").kind == CONSOLE_CMD_NONE);
    CHECK(console_parse_line("stacks").kind == CONSOLE_CMD_NONE);
    CHECK(console_parse_line("").kind == CONSOLE_CMD_NONE);
}

TEST_CASE("Console command: tap X Y", "[esp32][console]") {
    const console_cmd cmd = console_parse_line("tap 208 110");
    CHECK(cmd.kind == CONSOLE_CMD_TAP);
    CHECK(cmd.a == 208);
    CHECK(cmd.b == 110);

    const console_cmd spaced = console_parse_line("tap  0   32767");
    CHECK(spaced.kind == CONSOLE_CMD_TAP);
    CHECK(spaced.a == 0);
    CHECK(spaced.b == 32767);
}

TEST_CASE("Console command: down X Y and up", "[esp32][console]") {
    const console_cmd down = console_parse_line("down 518 271");
    CHECK(down.kind == CONSOLE_CMD_DOWN);
    CHECK(down.a == 518);
    CHECK(down.b == 271);
    CHECK(console_parse_line("up").kind == CONSOLE_CMD_UP);
    CHECK(console_parse_line("up ").kind == CONSOLE_CMD_NONE);
    CHECK(console_parse_line("down 1").kind == CONSOLE_CMD_NONE);
    CHECK(console_parse_line("down 1 2 3").kind == CONSOLE_CMD_NONE);
    CHECK(console_parse_line("down 1 32768").kind == CONSOLE_CMD_NONE);
}

TEST_CASE("Console command: snapline N", "[esp32][console]") {
    const console_cmd cmd = console_parse_line("snapline 1234");
    CHECK(cmd.kind == CONSOLE_CMD_SNAPLINE);
    CHECK(cmd.a == 1234);
    CHECK(console_parse_line("snapline 0").a == 0);
    CHECK(console_parse_line("snapline 2147483647").kind == CONSOLE_CMD_SNAPLINE);
}

TEST_CASE("Console command: garbage is no command", "[esp32][console]") {
    for (const char* line :
         {"tap", "tap 1", "tap 1 2 3", "tap 1 2x", "tap x 2", "tap1 2", "tap -1 5", "tap +1 5",
          "tap 1 -5", "snapline", "snapline ", "snapline x", "snapline5", "snapline 5 ",
          "snapline -1", "\x01\xff", "snapfoo 1"}) {
        INFO(line);
        CHECK(console_parse_line(line).kind == CONSOLE_CMD_NONE);
    }
}

TEST_CASE("Console command: out-of-range numbers are no command", "[esp32][console]") {
    CHECK(console_parse_line("tap 32768 1").kind == CONSOLE_CMD_NONE);
    CHECK(console_parse_line("tap 1 32768").kind == CONSOLE_CMD_NONE);
    CHECK(console_parse_line("snapline 2147483648").kind == CONSOLE_CMD_NONE);
    CHECK(console_parse_line("snapline 99999999999999999999").kind == CONSOLE_CMD_NONE);
}
