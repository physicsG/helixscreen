// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "qr_decoder.h"

#include "../catch_amalgamated.hpp"

using helix::QrDecoder;

// ============================================================================
// parse_spoolman_id tests
// ============================================================================

TEST_CASE("parse_spoolman_id: web+spoolman format", "[qr_decoder]") {
    REQUIRE(QrDecoder::parse_spoolman_id("web+spoolman:s-42") == 42);
}

TEST_CASE("parse_spoolman_id: SM:SPOOL format", "[qr_decoder]") {
    REQUIRE(QrDecoder::parse_spoolman_id("SM:SPOOL=123") == 123);
}

TEST_CASE("parse_spoolman_id: URL with /spool/ path", "[qr_decoder]") {
    REQUIRE(QrDecoder::parse_spoolman_id("https://spoolman.local/view/spool/7") == 7);
}

TEST_CASE("parse_spoolman_id: URL with trailing slash", "[qr_decoder]") {
    REQUIRE(QrDecoder::parse_spoolman_id("https://host/spool/99/") == 99);
}

TEST_CASE("parse_spoolman_id: URL with /spool/show/ path (issue #660)", "[qr_decoder]") {
    REQUIRE(QrDecoder::parse_spoolman_id("https://spoolman-hostname/spool/show/7") == 7);
    REQUIRE(QrDecoder::parse_spoolman_id("https://spoolman.local:7912/spool/show/42/") == 42);
    REQUIRE(QrDecoder::parse_spoolman_id("http://10.0.0.5/spool/show/123") == 123);
}

TEST_CASE("parse_spoolman_id: scanner with unshifted characters", "[qr_decoder]") {
    // USB barcode scanners that only use Caps Lock (not Shift) send
    // '=' instead of '+' and ';' instead of ':'
    REQUIRE(QrDecoder::parse_spoolman_id("web=spoolman;s-42") == 42);
    REQUIRE(QrDecoder::parse_spoolman_id("web=spoolman;s-97") == 97);

    // SM:SPOOL= format still works (camera reads correctly)
    REQUIRE(QrDecoder::parse_spoolman_id("SM:SPOOL=123") == 123);
}

TEST_CASE("parse_spoolman_id: rejects non-Spoolman text", "[qr_decoder]") {
    REQUIRE(QrDecoder::parse_spoolman_id("Hello World") == -1);
}

TEST_CASE("parse_spoolman_id: rejects empty string", "[qr_decoder]") {
    REQUIRE(QrDecoder::parse_spoolman_id("") == -1);
}

TEST_CASE("parse_spoolman_id: edge case spool ID 0", "[qr_decoder]") {
    REQUIRE(QrDecoder::parse_spoolman_id("web+spoolman:s-0") == 0);
}

TEST_CASE("parse_spoolman_id: rejects numeric overflow", "[qr_decoder]") {
    REQUIRE(QrDecoder::parse_spoolman_id("web+spoolman:s-99999999999999999") == -1);
    REQUIRE(QrDecoder::parse_spoolman_id("SM:SPOOL=99999999999999999") == -1);
}

// ============================================================================
// QrDecoder lifecycle tests
// ============================================================================

TEST_CASE("QrDecoder: decode on blank image returns failure", "[qr_decoder]") {
    QrDecoder decoder;
    std::vector<uint8_t> blank(32 * 32, 128);
    auto result = decoder.decode(blank.data(), 32, 32);
    REQUIRE_FALSE(result.success);
    REQUIRE(result.spool_id == -1);
}

TEST_CASE("QrDecoder: decode with null data returns failure", "[qr_decoder]") {
    QrDecoder decoder;
    auto result = decoder.decode(nullptr, 100, 100);
    REQUIRE_FALSE(result.success);
}

TEST_CASE("QrDecoder: decode with zero dimensions returns failure", "[qr_decoder]") {
    QrDecoder decoder;
    uint8_t dummy[4] = {0};
    auto result = decoder.decode(dummy, 0, 0);
    REQUIRE_FALSE(result.success);
}

TEST_CASE("QrDecoder: decode with blank image returns failure", "[qr_decoder]") {
    QrDecoder decoder;
    // A small all-white image should not contain a QR code
    std::vector<uint8_t> blank(64 * 64, 255);
    auto result = decoder.decode(blank.data(), 64, 64);
    REQUIRE_FALSE(result.success);
    REQUIRE(result.spool_id == -1);
}
