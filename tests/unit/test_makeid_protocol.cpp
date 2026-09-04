// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "makeid_protocol.h"

#include <cstring>

#include "../catch_amalgamated.hpp"

using namespace helix::label;

// ============================================================================
// Frame building
// ============================================================================

TEST_CASE("makeid_build_frame - handshake frame structure", "[makeid][protocol]") {
    auto frame = makeid_build_handshake(MakeIdHandshakeState::Search);

    // [0x66 LEN_LO LEN_HI CMD STATE CHECKSUM]
    REQUIRE(frame.size() == 6);
    REQUIRE(frame[0] == 0x66);                                               // magic
    REQUIRE(frame[1] == 0x06);                                               // length low
    REQUIRE(frame[2] == 0x00);                                               // length high
    REQUIRE(frame[3] == static_cast<uint8_t>(MakeIdCmd::Handshake));         // 0x10
    REQUIRE(frame[4] == static_cast<uint8_t>(MakeIdHandshakeState::Search)); // 0x00
}

TEST_CASE("makeid_build_frame - checksum is negated sum", "[makeid][protocol]") {
    auto frame = makeid_build_handshake(MakeIdHandshakeState::Search);

    // checksum = -(sum of bytes[0..N-2]) & 0xFF
    uint8_t sum = 0;
    for (size_t i = 0; i < frame.size() - 1; i++) {
        sum += frame[i];
    }
    uint8_t expected_checksum = static_cast<uint8_t>(-sum);
    REQUIRE(frame.back() == expected_checksum);
}

TEST_CASE("makeid_build_frame - cancel handshake", "[makeid][protocol]") {
    auto frame = makeid_build_handshake(MakeIdHandshakeState::Cancel);

    REQUIRE(frame.size() == 6);
    REQUIRE(frame[4] == static_cast<uint8_t>(MakeIdHandshakeState::Cancel)); // 0x03

    // Verify checksum
    uint8_t sum = 0;
    for (size_t i = 0; i < frame.size() - 1; i++) {
        sum += frame[i];
    }
    REQUIRE(frame.back() == static_cast<uint8_t>(-sum));
}

// ============================================================================
// Checksum
// ============================================================================

TEST_CASE("makeid_checksum - known values", "[makeid][protocol]") {
    // Handshake search: [0x66, 0x06, 0x00, 0x10, 0x00] -> sum = 0x7C -> checksum = 0x84
    std::vector<uint8_t> data = {0x66, 0x06, 0x00, 0x10, 0x00};
    REQUIRE(makeid_checksum(data.data(), data.size()) ==
            static_cast<uint8_t>(-(0x66 + 0x06 + 0x00 + 0x10 + 0x00)));
}

// ============================================================================
// Response parsing
// ============================================================================

TEST_CASE("makeid_parse_response - success", "[makeid][protocol]") {
    // Build a 36-byte response with error_code=0, no flags
    std::vector<uint8_t> resp(36, 0x00);
    resp[3] = 0x10; // response to handshake

    auto result = makeid_parse_response(resp.data(), resp.size());
    REQUIRE(result.status == MakeIdResponseStatus::Success);
    REQUIRE(result.error_code == 0);
}

TEST_CASE("makeid_parse_response - wait flag", "[makeid][protocol]") {
    std::vector<uint8_t> resp(36, 0x00);
    resp[4] = 0x80; // bit 7 = wait

    auto result = makeid_parse_response(resp.data(), resp.size());
    REQUIRE(result.status == MakeIdResponseStatus::Wait);
}

TEST_CASE("makeid_parse_response - resend flag", "[makeid][protocol]") {
    std::vector<uint8_t> resp(36, 0x00);
    resp[4] = 0x40; // bit 6 = resend

    auto result = makeid_parse_response(resp.data(), resp.size());
    REQUIRE(result.status == MakeIdResponseStatus::Resend);
}

TEST_CASE("makeid_parse_response - error code", "[makeid][protocol]") {
    std::vector<uint8_t> resp(36, 0x00);
    resp[4] = 0x03; // error code 3 = label over

    auto result = makeid_parse_response(resp.data(), resp.size());
    REQUIRE(result.status == MakeIdResponseStatus::Error);
    REQUIRE(result.error_code == 3);
}

TEST_CASE("makeid_parse_response - error code 23 is success", "[makeid][protocol]") {
    std::vector<uint8_t> resp(36, 0x00);
    resp[4] = 23; // error code 23 = no error (same as 0)

    auto result = makeid_parse_response(resp.data(), resp.size());
    REQUIRE(result.status == MakeIdResponseStatus::Success);
}

TEST_CASE("makeid_parse_response - too short returns resend", "[makeid][protocol]") {
    std::vector<uint8_t> resp(10, 0x00);

    auto result = makeid_parse_response(resp.data(), resp.size());
    REQUIRE(result.status == MakeIdResponseStatus::Resend);
}

TEST_CASE("makeid_parse_response - null response (0x11)", "[makeid][protocol]") {
    std::vector<uint8_t> resp(8, 0x00);
    resp[3] = 0x11; // null response marker

    auto result = makeid_parse_response(resp.data(), resp.size());
    REQUIRE(result.status == MakeIdResponseStatus::Null);
}

TEST_CASE("makeid_parse_response - is_printing flag", "[makeid][protocol]") {
    std::vector<uint8_t> resp(36, 0x00);
    resp[35] = 0x80; // bit 7 = isPrinting

    auto result = makeid_parse_response(resp.data(), resp.size());
    REQUIRE(result.is_printing);
}

TEST_CASE("makeid_parse_response - cancel state", "[makeid][protocol]") {
    std::vector<uint8_t> resp(36, 0x00);
    resp[35] = 0x60; // bits 5-6 = 3 = cancel

    auto result = makeid_parse_response(resp.data(), resp.size());
    REQUIRE(result.status == MakeIdResponseStatus::Exit);
}

TEST_CASE("makeid_parse_response - pause state", "[makeid][protocol]") {
    std::vector<uint8_t> resp(36, 0x00);
    resp[35] = 0x20; // bits 5-6 = 1 = pause

    auto result = makeid_parse_response(resp.data(), resp.size());
    REQUIRE(result.status == MakeIdResponseStatus::Pause);
}

// ============================================================================
// Print data frame (0x1B)
// ============================================================================

TEST_CASE("makeid_build_print_frame - header structure", "[makeid][protocol]") {
    // Create a small bitmap chunk: 8 pixels wide (1 byte), 4 rows
    std::vector<uint8_t> bitmap_data(4, 0xFF); // 4 bytes = 1 byte/row * 4 rows

    MakeIdPrintParams params;
    params.darkness = 15;
    params.total_copies = 1;
    params.current_copy = 1;
    params.width_px = 96;
    params.chunk_height = 4;
    params.remaining_chunks = 0;

    auto frame = makeid_build_print_frame(bitmap_data, params);

    REQUIRE(frame.size() >= 18); // 17 header + at least 1 byte payload + checksum
    REQUIRE(frame[0] == 0x66);   // magic
    REQUIRE(frame[3] == 0x1B);   // print data command

    // Darkness in bits 0-4 of byte 4
    REQUIRE((frame[4] & 0x1F) == 15);

    // Width in bytes 11-12 (little-endian)
    uint16_t width = frame[11] | (frame[12] << 8);
    REQUIRE(width == 96);

    // Height in bytes 13-14 (little-endian)
    uint16_t height = frame[13] | (frame[14] << 8);
    REQUIRE(height == 4);

    // Remaining chunks in byte 15
    REQUIRE(frame[15] == 0);

    // Byte 16 = 0x00 reserved
    REQUIRE(frame[16] == 0x00);

    // Last byte is checksum
    uint8_t sum = 0;
    for (size_t i = 0; i < frame.size() - 1; i++) {
        sum += frame[i];
    }
    REQUIRE(frame.back() == static_cast<uint8_t>(-sum));
}

TEST_CASE("makeid_build_print_frame - length field is correct", "[makeid][protocol]") {
    std::vector<uint8_t> bitmap_data(10, 0xAA);

    MakeIdPrintParams params;
    params.darkness = 5;
    params.total_copies = 1;
    params.current_copy = 1;
    params.width_px = 80;
    params.chunk_height = 10;
    params.remaining_chunks = 2;

    auto frame = makeid_build_print_frame(bitmap_data, params);

    // Total frame length stored in bytes 1-2 (LE)
    uint16_t stored_len = frame[1] | (frame[2] << 8);
    REQUIRE(stored_len == frame.size());
}

TEST_CASE("makeid_build_print_frame - large frame length uses both bytes", "[makeid][protocol]") {
    // Create a payload big enough that total frame > 255
    std::vector<uint8_t> bitmap_data(300, 0x55);

    MakeIdPrintParams params;
    params.width_px = 96;
    params.chunk_height = 25;
    params.remaining_chunks = 0;

    auto frame = makeid_build_print_frame(bitmap_data, params);

    uint16_t stored_len = frame[1] | (frame[2] << 8);
    REQUIRE(stored_len == frame.size());
    REQUIRE(stored_len > 255);
    REQUIRE(frame[2] > 0); // high byte must be non-zero
}

// ============================================================================
// Bitmap encoding (1bpp column-major)
// ============================================================================

TEST_CASE("makeid_encode_bitmap - basic MSB-first packing", "[makeid][protocol]") {
    // 16px wide, 1 row — LabelBitmap already does MSB-first
    LabelBitmap bmp(16, 1);
    // Set first 8 pixels black
    for (int x = 0; x < 8; x++) {
        bmp.set_pixel(x, 0, true);
    }

    int printer_width_bytes = 2;
    auto chunks = makeid_encode_bitmap(bmp, printer_width_bytes, 56);

    REQUIRE(chunks.size() == 1);

    // Column-major memcpy: [0xFF, 0x00]
    auto& chunk = chunks[0];
    REQUIRE(chunk.height == 1);
    REQUIRE(chunk.data.size() >= 2); // may be LZO-compressed, but at minimum 2 bytes input

    // We test the raw (pre-LZO) encoding via a separate helper
}

TEST_CASE("makeid_encode_bitmap_raw - column-major memcpy", "[makeid][protocol]") {
    // 16px wide, 1 row
    LabelBitmap bmp(16, 1);
    // First 8 pixels black: byte[0]=0xFF, byte[1]=0x00
    for (int x = 0; x < 8; x++) {
        bmp.set_pixel(x, 0, true);
    }

    int printer_width_bytes = 2;
    auto raw = makeid_encode_bitmap_raw(bmp, printer_width_bytes);

    // Plain memcpy, no byte-swap: [0xFF, 0x00]
    REQUIRE(raw.size() == 2);
    REQUIRE(raw[0] == 0xFF);
    REQUIRE(raw[1] == 0x00);
}

TEST_CASE("makeid_encode_bitmap_raw - odd width padding", "[makeid][protocol]") {
    // 12px wide bitmap, but printer_width_bytes=2 (16px)
    LabelBitmap bmp(12, 1);
    for (int x = 0; x < 12; x++) {
        bmp.set_pixel(x, 0, true);
    }

    int printer_width_bytes = 2;
    auto raw = makeid_encode_bitmap_raw(bmp, printer_width_bytes);

    REQUIRE(raw.size() == 2);
    // Plain memcpy: byte[0]=0xFF, byte[1]=0xF0 (12 bits set, 4 padding zeros)
    REQUIRE(raw[0] == 0xFF);
    REQUIRE(raw[1] == 0xF0);
}

TEST_CASE("makeid_encode_bitmap_raw - multi-row", "[makeid][protocol]") {
    LabelBitmap bmp(16, 2);
    // Row 0: all black
    memset(bmp.row_data(0), 0xFF, 2);
    // Row 1: all white (default)

    int printer_width_bytes = 2;
    auto raw = makeid_encode_bitmap_raw(bmp, printer_width_bytes);

    // 2 rows * 2 bytes = 4 bytes
    REQUIRE(raw.size() == 4);
    // Row 0: [0xFF, 0xFF]
    REQUIRE(raw[0] == 0xFF);
    REQUIRE(raw[1] == 0xFF);
    // Row 1: [0x00, 0x00]
    REQUIRE(raw[2] == 0x00);
    REQUIRE(raw[3] == 0x00);
}

// ============================================================================
// Chunking
// ============================================================================

TEST_CASE("makeid_encode_bitmap - splits tall images into chunks", "[makeid][protocol]") {
    // 16px wide, 120 rows — should split into 3 chunks (56+56+8) at max_rows=56
    LabelBitmap bmp(16, 120);

    int printer_width_bytes = 2;
    auto chunks = makeid_encode_bitmap(bmp, printer_width_bytes, 56);

    REQUIRE(chunks.size() == 3);
    REQUIRE(chunks[0].height == 56);
    REQUIRE(chunks[1].height == 56);
    REQUIRE(chunks[2].height == 8);
}

TEST_CASE("makeid_encode_bitmap - single chunk for short image", "[makeid][protocol]") {
    LabelBitmap bmp(16, 30);

    int printer_width_bytes = 2;
    auto chunks = makeid_encode_bitmap(bmp, printer_width_bytes, 56);

    REQUIRE(chunks.size() == 1);
    REQUIRE(chunks[0].height == 30);
}

// ============================================================================
// Label sizes
// ============================================================================

TEST_CASE("makeid_e1_sizes - returns valid sizes", "[makeid][protocol]") {
    auto sizes = makeid_e1_sizes();
    REQUIRE(!sizes.empty());
    for (const auto& s : sizes) {
        REQUIRE(s.dpi == 203);
        REQUIRE(s.width_px > 0);
        REQUIRE(s.height_px > 0);
    }
}

TEST_CASE("makeid_default_sizes - returns valid sizes", "[makeid][protocol]") {
    auto sizes = makeid_default_sizes();
    REQUIRE(!sizes.empty());
    for (const auto& s : sizes) {
        REQUIRE(s.dpi > 0);
        REQUIRE(s.width_px > 0);
    }
}

// ============================================================================
// Integration: full print job
// ============================================================================

TEST_CASE("makeid_build_print_job - produces valid frame sequence", "[makeid][protocol]") {
    LabelBitmap bmp(96, 40);
    // Put some data in
    for (int y = 0; y < 40; y++) {
        for (int x = 0; x < 48; x++) {
            bmp.set_pixel(x, y, (x + y) % 2 == 0);
        }
    }

    LabelSize size{"12x40mm", 96, 307, 203, 0x01, 12, 40};

    MakeIdPrintJobConfig config;
    config.darkness = 10;
    config.printer_width_bytes = 12; // 96px / 8
    config.max_rows_per_chunk = 56;

    auto job = makeid_build_print_job(bmp, size, config);

    // Should have at least one chunk
    REQUIRE(!job.chunks.empty());
    REQUIRE(job.total_rows == 40);

    // Each chunk frame should start with 0x66 and have cmd 0x1B
    for (const auto& frame : job.chunks) {
        REQUIRE(frame[0] == 0x66);
        REQUIRE(frame[3] == 0x1B);

        // Verify checksum
        uint8_t sum = 0;
        for (size_t i = 0; i < frame.size() - 1; i++) {
            sum += frame[i];
        }
        REQUIRE(frame.back() == static_cast<uint8_t>(-sum));
    }
}

TEST_CASE("makeid_build_print_job - 40-row image fits in one chunk", "[makeid][protocol]") {
    LabelBitmap bmp(96, 40);
    LabelSize size{"12x40mm", 96, 307, 203, 0x01, 12, 40};

    MakeIdPrintJobConfig config;
    config.printer_width_bytes = 12;
    config.max_rows_per_chunk = 56;

    auto job = makeid_build_print_job(bmp, size, config);

    REQUIRE(job.chunks.size() == 1);

    // Remaining chunks byte (offset 15) should be 0
    REQUIRE(job.chunks[0][15] == 0);
}

TEST_CASE(
    "makeid_build_print_job - tall image produces multiple chunks with correct remaining count",
    "[makeid][protocol]") {
    LabelBitmap bmp(96, 120);
    LabelSize size{"12x100mm", 96, 800, 203, 0x01, 12, 100};

    MakeIdPrintJobConfig config;
    config.printer_width_bytes = 12;
    config.max_rows_per_chunk = 56;

    auto job = makeid_build_print_job(bmp, size, config);

    // 120 rows / 56 max = 3 chunks (56+56+8)
    REQUIRE(job.chunks.size() == 3);

    // First chunk: remaining = 2
    REQUIRE(job.chunks[0][15] == 2);
    // Second chunk: remaining = 1
    REQUIRE(job.chunks[1][15] == 1);
    // Third chunk: remaining = 0
    REQUIRE(job.chunks[2][15] == 0);
}
