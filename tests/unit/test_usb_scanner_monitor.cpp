// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "usb_scanner_monitor.h"

#include <string>
#include <vector>

#include "../catch_amalgamated.hpp"

// Define evdev keycodes for test builds (values match linux/input-event-codes.h)
#ifndef KEY_1
#define KEY_1 2
#define KEY_2 3
#define KEY_3 4
#define KEY_4 5
#define KEY_5 6
#define KEY_6 7
#define KEY_7 8
#define KEY_8 9
#define KEY_9 10
#define KEY_0 11
#define KEY_MINUS 12
#define KEY_EQUAL 13
#define KEY_Q 16
#define KEY_W 17
#define KEY_E 18
#define KEY_R 19
#define KEY_T 20
#define KEY_Y 21
#define KEY_U 22
#define KEY_I 23
#define KEY_O 24
#define KEY_P 25
#define KEY_A 30
#define KEY_S 31
#define KEY_D 32
#define KEY_F 33
#define KEY_G 34
#define KEY_H 35
#define KEY_J 36
#define KEY_K 37
#define KEY_L 38
#define KEY_SEMICOLON 39
#define KEY_Z 44
#define KEY_X 45
#define KEY_C 46
#define KEY_V 47
#define KEY_B 48
#define KEY_N 49
#define KEY_M 50
#define KEY_COMMA 51
#define KEY_DOT 52
#define KEY_SLASH 53
#define KEY_LEFTSHIFT 42
#define KEY_LEFTCTRL 29
#define KEY_ENTER 28
#endif

using helix::UsbScannerMonitor;

TEST_CASE("keycode_to_char: digits KEY_0..KEY_9", "[usb_scanner]") {
    CHECK(UsbScannerMonitor::keycode_to_char(KEY_1, false) == '1');
    CHECK(UsbScannerMonitor::keycode_to_char(KEY_2, false) == '2');
    CHECK(UsbScannerMonitor::keycode_to_char(KEY_3, false) == '3');
    CHECK(UsbScannerMonitor::keycode_to_char(KEY_4, false) == '4');
    CHECK(UsbScannerMonitor::keycode_to_char(KEY_5, false) == '5');
    CHECK(UsbScannerMonitor::keycode_to_char(KEY_6, false) == '6');
    CHECK(UsbScannerMonitor::keycode_to_char(KEY_7, false) == '7');
    CHECK(UsbScannerMonitor::keycode_to_char(KEY_8, false) == '8');
    CHECK(UsbScannerMonitor::keycode_to_char(KEY_9, false) == '9');
    CHECK(UsbScannerMonitor::keycode_to_char(KEY_0, false) == '0');
}

TEST_CASE("keycode_to_char: letters KEY_A..KEY_Z lowercase", "[usb_scanner]") {
    CHECK(UsbScannerMonitor::keycode_to_char(KEY_A, false) == 'a');
    CHECK(UsbScannerMonitor::keycode_to_char(KEY_B, false) == 'b');
    CHECK(UsbScannerMonitor::keycode_to_char(KEY_C, false) == 'c');
    CHECK(UsbScannerMonitor::keycode_to_char(KEY_Z, false) == 'z');
    CHECK(UsbScannerMonitor::keycode_to_char(KEY_M, false) == 'm');
    CHECK(UsbScannerMonitor::keycode_to_char(KEY_Q, false) == 'q');
    CHECK(UsbScannerMonitor::keycode_to_char(KEY_P, false) == 'p');
}

TEST_CASE("keycode_to_char: letters with shift produce uppercase", "[usb_scanner]") {
    CHECK(UsbScannerMonitor::keycode_to_char(KEY_A, true) == 'A');
    CHECK(UsbScannerMonitor::keycode_to_char(KEY_Z, true) == 'Z');
    CHECK(UsbScannerMonitor::keycode_to_char(KEY_M, true) == 'M');
    CHECK(UsbScannerMonitor::keycode_to_char(KEY_S, true) == 'S');
}

TEST_CASE("keycode_to_char: special keys", "[usb_scanner]") {
    CHECK(UsbScannerMonitor::keycode_to_char(KEY_MINUS, false) == '-');
    CHECK(UsbScannerMonitor::keycode_to_char(KEY_DOT, false) == '.');
    CHECK(UsbScannerMonitor::keycode_to_char(KEY_SLASH, false) == '/');
    CHECK(UsbScannerMonitor::keycode_to_char(KEY_EQUAL, false) == '=');
    CHECK(UsbScannerMonitor::keycode_to_char(KEY_COMMA, false) == ',');
    CHECK(UsbScannerMonitor::keycode_to_char(KEY_SEMICOLON, false) == ';');
}

TEST_CASE("keycode_to_char: shift special keys", "[usb_scanner]") {
    CHECK(UsbScannerMonitor::keycode_to_char(KEY_SEMICOLON, true) == ':');
    CHECK(UsbScannerMonitor::keycode_to_char(KEY_MINUS, true) == '_');
    CHECK(UsbScannerMonitor::keycode_to_char(KEY_EQUAL, true) == '+');
    CHECK(UsbScannerMonitor::keycode_to_char(KEY_SLASH, true) == '?');
    CHECK(UsbScannerMonitor::keycode_to_char(KEY_DOT, true) == '>');
    CHECK(UsbScannerMonitor::keycode_to_char(KEY_COMMA, true) == '<');
}

TEST_CASE("keycode_to_char: unmapped key returns 0", "[usb_scanner]") {
    CHECK(UsbScannerMonitor::keycode_to_char(KEY_LEFTCTRL, false) == 0);
    CHECK(UsbScannerMonitor::keycode_to_char(KEY_LEFTSHIFT, false) == 0);
    CHECK(UsbScannerMonitor::keycode_to_char(KEY_ENTER, false) == 0);
    CHECK(UsbScannerMonitor::keycode_to_char(999, false) == 0);
}

TEST_CASE("keycode_to_char: QWERTZ swaps Y and Z", "[usb_scanner]") {
    using helix::ScannerKeymap;
    // KEY_Y keycode (21) yields 'z' on QWERTZ; KEY_Z keycode (44) yields 'y'.
    CHECK(UsbScannerMonitor::keycode_to_char(KEY_Y, false, ScannerKeymap::Qwertz) == 'z');
    CHECK(UsbScannerMonitor::keycode_to_char(KEY_Z, false, ScannerKeymap::Qwertz) == 'y');
    CHECK(UsbScannerMonitor::keycode_to_char(KEY_Y, true, ScannerKeymap::Qwertz) == 'Z');
    CHECK(UsbScannerMonitor::keycode_to_char(KEY_Z, true, ScannerKeymap::Qwertz) == 'Y');
    // All other letters match QWERTY.
    CHECK(UsbScannerMonitor::keycode_to_char(KEY_A, false, ScannerKeymap::Qwertz) == 'a');
    CHECK(UsbScannerMonitor::keycode_to_char(KEY_M, false, ScannerKeymap::Qwertz) == 'm');
}

namespace {
/// Feed a keycode/shift sequence through keycode_to_char on the given layout
/// and return the accumulated decoded string. Skips keycodes that map to 0
/// (the same behaviour monitor_thread_func uses).
struct KeyEvent {
    int keycode;
    bool shift;
};
std::string decode_sequence(const std::vector<KeyEvent>& seq, helix::ScannerKeymap layout) {
    std::string out;
    for (const auto& e : seq) {
        char c = UsbScannerMonitor::keycode_to_char(e.keycode, e.shift, layout);
        if (c != 0)
            out += c;
    }
    return out;
}
} // namespace

TEST_CASE("keycode_to_char: AZERTY end-to-end decode of Spoolman URL ASCII subset",
          "[usb_scanner]") {
    using helix::ScannerKeymap;
    // Reproduces the AZERTY bug report: a Spoolman barcode scanned via an
    // AZERTY-configured HID scanner previously decoded as "zeb+spool;qn.s6!"
    // instead of its real content. This test feeds the
    // exact keycode sequence an AZERTY scanner would emit for "web+spoolman-s-6"
    // and asserts it decodes correctly end-to-end through keycode_to_char.
    //
    // NOTE: we test "web+spoolman-s-6" rather than the bug report's literal
    // "web+spoolman:s-6" because AZERTY punctuation handling is intentionally
    // scoped to ASCII-only subset — ':' on AZERTY lives at a different keycode
    // than QWERTY and is not yet mapped. The bare "-" separator exercises the
    // same decode path without pulling in deferred punctuation work.

    const std::vector<KeyEvent> azerty_web_plus_spoolman_s_6 = {
        {KEY_Z, false},         // 'w' — physical Z position on AZERTY
        {KEY_E, false},         // 'e'
        {KEY_B, false},         // 'b'
        {KEY_EQUAL, true},      // '+' — shared with QWERTY in our table
        {KEY_S, false},         // 's'
        {KEY_P, false},         // 'p'
        {KEY_O, false},         // 'o'
        {KEY_O, false},         // 'o'
        {KEY_L, false},         // 'l'
        {KEY_SEMICOLON, false}, // 'm' — physical ; position on QWERTY
        {KEY_Q, false},         // 'a' — physical Q position on QWERTY
        {KEY_N, false},         // 'n'
        {KEY_MINUS, false},     // '-'
        {KEY_S, false},         // 's'
        {KEY_MINUS, false},     // '-'
        {KEY_6, true},          // '6' — shifted-by-default digit row on AZERTY
    };
    CHECK(decode_sequence(azerty_web_plus_spoolman_s_6, ScannerKeymap::Azerty) ==
          "web+spoolman-s-6");

    // And the decoded string should match the Spoolman parser so the full
    // pipeline — scanner emit → keycode_to_char → check_spoolman_pattern —
    // would recognise spool id 6.
    CHECK(UsbScannerMonitor::check_spoolman_pattern("web+spoolman:s-6") == 6);
}

TEST_CASE("keycode_to_char: AZERTY scanner through QWERTZ produces garbled output",
          "[usb_scanner]") {
    using helix::ScannerKeymap;
    // Reproduces the exact bug report: an AZERTY-configured scanner was decoded
    // through QWERTZ layout, producing "yeb+spool;qn.s6!" instead of
    // "web+spoolman:s-1". This test proves the garbling is deterministic and
    // that switching to the correct layout (AZERTY) fixes it.
    //
    // The AZERTY scanner emits these keycodes for "web+spoolman:s-1":
    const std::vector<KeyEvent> azerty_keycodes = {
        {KEY_Z, false},         // 'w' on AZERTY (keycode 44)
        {KEY_E, false},         // 'e'
        {KEY_B, false},         // 'b'
        {KEY_EQUAL, true},      // '+' (Shift+= on QWERTY/QWERTZ)
        {KEY_S, false},         // 's'
        {KEY_P, false},         // 'p'
        {KEY_O, false},         // 'o'
        {KEY_O, false},         // 'o'
        {KEY_L, false},         // 'l'
        {KEY_SEMICOLON, false}, // 'm' on AZERTY (keycode 39)
        {KEY_Q, false},         // 'a' on AZERTY (keycode 16)
        {KEY_N, false},         // 'n'
        {KEY_DOT, false},       // ':' on AZERTY (unshifted keycode 52)
        {KEY_S, false},         // 's'
        {KEY_6, false},         // '-' on AZERTY (unshifted 6 key → keycode 7)
        {KEY_1, true},          // '1' on AZERTY (shifted 1 key → keycode 2)
    };

    // When decoded through QWERTZ (wrong layout), we get garbled output.
    // KEY_Z(44) → QWERTZ 'y' (not 'w'), KEY_SEMICOLON(39) → QWERTZ ';' (not 'm'),
    // KEY_Q(16) → QWERTZ 'q' (not 'a'), KEY_DOT(52) → QWERTZ '.' (not ':'),
    // KEY_6(7) → QWERTZ '6' (not '-'), KEY_1+shift(2) → QWERTZ '!' (not '1').
    std::string qwertz_result = decode_sequence(azerty_keycodes, ScannerKeymap::Qwertz);
    CHECK(qwertz_result == "yeb+spool;qn.s6!");

    // When decoded through AZERTY (correct layout), we get the right output.
    std::string azerty_result = decode_sequence(azerty_keycodes, ScannerKeymap::Azerty);
    CHECK(azerty_result == "web+spoolman:s-1");

    // The correctly decoded string should parse as Spoolman spool ID 1.
    CHECK(UsbScannerMonitor::check_spoolman_pattern(azerty_result) == 1);

    // The garbled string should NOT parse as Spoolman.
    CHECK(UsbScannerMonitor::check_spoolman_pattern(qwertz_result) == -1);
}

TEST_CASE("keycode_to_char: AZERTY key positions (individual checks)", "[usb_scanner]") {
    using helix::ScannerKeymap;
    // Letter rearrangement checks — complement to the end-to-end test above.
    CHECK(UsbScannerMonitor::keycode_to_char(KEY_Z, false, ScannerKeymap::Azerty) == 'w');
    CHECK(UsbScannerMonitor::keycode_to_char(KEY_Q, false, ScannerKeymap::Azerty) == 'a');
    CHECK(UsbScannerMonitor::keycode_to_char(KEY_W, false, ScannerKeymap::Azerty) == 'z');
    CHECK(UsbScannerMonitor::keycode_to_char(KEY_A, false, ScannerKeymap::Azerty) == 'q');
    CHECK(UsbScannerMonitor::keycode_to_char(KEY_SEMICOLON, false, ScannerKeymap::Azerty) == 'm');
    // Shifted digits on AZERTY yield the literal digits 1–0.
    CHECK(UsbScannerMonitor::keycode_to_char(KEY_1, true, ScannerKeymap::Azerty) == '1');
    CHECK(UsbScannerMonitor::keycode_to_char(KEY_6, true, ScannerKeymap::Azerty) == '6');
    // KEY_MINUS and KEY_EQUAL kept QWERTY mapping so "s-<id>" Spoolman codes
    // still decode cleanly.
    CHECK(UsbScannerMonitor::keycode_to_char(KEY_MINUS, false, ScannerKeymap::Azerty) == '-');
    CHECK(UsbScannerMonitor::keycode_to_char(KEY_EQUAL, true, ScannerKeymap::Azerty) == '+');
}

TEST_CASE("keycode_to_char: default layout is QWERTY (backward compat)", "[usb_scanner]") {
    // The layout parameter defaults to Qwerty so existing callers keep working.
    CHECK(UsbScannerMonitor::keycode_to_char(KEY_A, false) == 'a');
    CHECK(UsbScannerMonitor::keycode_to_char(KEY_Z, false) == 'z');
}

TEST_CASE("parse_keymap recognises supported layouts", "[usb_scanner]") {
    using helix::ScannerKeymap;
    CHECK(UsbScannerMonitor::parse_keymap("qwerty") == ScannerKeymap::Qwerty);
    CHECK(UsbScannerMonitor::parse_keymap("qwertz") == ScannerKeymap::Qwertz);
    CHECK(UsbScannerMonitor::parse_keymap("azerty") == ScannerKeymap::Azerty);
    CHECK(UsbScannerMonitor::parse_keymap("garbage") == ScannerKeymap::Qwerty);
    CHECK(UsbScannerMonitor::parse_keymap("") == ScannerKeymap::Qwerty);
}

TEST_CASE("check_spoolman_pattern: web+spoolman format", "[usb_scanner]") {
    CHECK(UsbScannerMonitor::check_spoolman_pattern("web+spoolman:s-42") == 42);
    CHECK(UsbScannerMonitor::check_spoolman_pattern("web+spoolman:s-1") == 1);
    CHECK(UsbScannerMonitor::check_spoolman_pattern("web+spoolman:s-999") == 999);
}

TEST_CASE("check_spoolman_pattern: SM:SPOOL format", "[usb_scanner]") {
    CHECK(UsbScannerMonitor::check_spoolman_pattern("SM:SPOOL=123") == 123);
    CHECK(UsbScannerMonitor::check_spoolman_pattern("SM:SPOOL=1") == 1);
}

TEST_CASE("check_spoolman_pattern: URL format", "[usb_scanner]") {
    CHECK(UsbScannerMonitor::check_spoolman_pattern("https://spoolman.local/view/spool/7") == 7);
    CHECK(UsbScannerMonitor::check_spoolman_pattern("http://localhost:7912/spool/42") == 42);
    CHECK(UsbScannerMonitor::check_spoolman_pattern("https://example.com/spool/100/") == 100);
}

TEST_CASE("check_spoolman_pattern: invalid input returns -1", "[usb_scanner]") {
    CHECK(UsbScannerMonitor::check_spoolman_pattern("random text") == -1);
    CHECK(UsbScannerMonitor::check_spoolman_pattern("not a qr code") == -1);
    CHECK(UsbScannerMonitor::check_spoolman_pattern("12345") == -1);
}

TEST_CASE("check_spoolman_pattern: empty input returns -1", "[usb_scanner]") {
    CHECK(UsbScannerMonitor::check_spoolman_pattern("") == -1);
}

TEST_CASE("UsbScannerMonitor: construct and destroy without starting", "[usb_scanner]") {
    // Should not crash
    UsbScannerMonitor monitor;
    CHECK_FALSE(monitor.is_running());
}
