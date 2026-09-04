// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "niimbot_protocol.h"

#include <cstring>

#include "../catch_amalgamated.hpp"

using namespace helix;
using namespace helix::label;

// ============================================================================
// Packet building
// ============================================================================

TEST_CASE("niimbot_build_packet - basic structure", "[niimbot][protocol]") {
    auto pkt = niimbot_build_packet(NiimbotCmd::Heartbeat, uint8_t(0x01));

    // [0x55 0x55 CMD LEN DATA CHECKSUM 0xAA 0xAA]
    REQUIRE(pkt.size() == 8);
    REQUIRE(pkt[0] == 0x55);
    REQUIRE(pkt[1] == 0x55);
    REQUIRE(pkt[2] == 0xDC); // Heartbeat
    REQUIRE(pkt[3] == 0x01); // length
    REQUIRE(pkt[4] == 0x01); // data
    // checksum = 0xDC ^ 0x01 ^ 0x01 = 0xDC
    REQUIRE(pkt[5] == 0xDC);
    REQUIRE(pkt[6] == 0xAA);
    REQUIRE(pkt[7] == 0xAA);
}

TEST_CASE("niimbot_build_packet - tail is two bytes", "[niimbot][protocol]") {
    auto pkt = niimbot_build_packet(NiimbotCmd::PrintEnd, uint8_t(0x01));
    REQUIRE(pkt.size() == 8); // 2 header + 1 cmd + 1 len + 1 data + 1 checksum + 2 tail
    REQUIRE(pkt[pkt.size() - 2] == 0xAA);
    REQUIRE(pkt[pkt.size() - 1] == 0xAA);
}

TEST_CASE("niimbot_build_packet - checksum is XOR of cmd, len, data", "[niimbot][protocol]") {
    // SetDensity(3): cmd=0x21, len=0x01, data=0x03
    // checksum = 0x21 ^ 0x01 ^ 0x03 = 0x23
    auto pkt = niimbot_build_packet(NiimbotCmd::SetDensity, uint8_t(0x03));
    REQUIRE(pkt[2] == 0x21); // cmd
    REQUIRE(pkt[3] == 0x01); // len
    REQUIRE(pkt[4] == 0x03); // data
    REQUIRE(pkt[5] == 0x23); // checksum
}

TEST_CASE("niimbot_build_packet - empty data", "[niimbot][protocol]") {
    auto pkt = niimbot_build_packet(NiimbotCmd::PageStart, nullptr, 0);
    REQUIRE(pkt.size() == 7); // header(2) + cmd(1) + len(1) + checksum(1) + tail(2)
    REQUIRE(pkt[3] == 0x00);  // length = 0
    // checksum = cmd ^ 0 = cmd
    REQUIRE(pkt[4] == static_cast<uint8_t>(NiimbotCmd::PageStart));
}

TEST_CASE("niimbot_build_packet - multi-byte data", "[niimbot][protocol]") {
    uint8_t data[] = {0x00, 0x64, 0x01, 0x80}; // page size: height=100, width=384
    auto pkt = niimbot_build_packet(NiimbotCmd::SetPageSize, data, 4);
    REQUIRE(pkt.size() == 11); // 2+1+1+4+1+2
    REQUIRE(pkt[2] == 0x13);   // SetPageSize
    REQUIRE(pkt[3] == 0x04);   // length
}

// ============================================================================
// Print job building
// ============================================================================

TEST_CASE("niimbot_build_print_job - produces correct packet sequence", "[niimbot][protocol]") {
    // Create a small 8x4 bitmap (1 byte wide, 4 rows)
    LabelBitmap bmp(8, 4);
    // Fill alternating rows: row 0 = all black, row 1 = white, row 2 = black, row 3 = white
    memset(bmp.row_data(0), 0xFF, bmp.row_byte_width());
    // row 1 is already 0x00
    memset(bmp.row_data(2), 0xFF, bmp.row_byte_width());
    // row 3 is already 0x00

    LabelSize size{"15x30mm", 96, 231, 203, 0x01, 15, 30};

    auto job = niimbot_build_print_job(bmp, size);

    // Should have: SetDensity, SetLabelType, PrintStart, PrintClear, PageStart,
    // SetPageSize, SetQuantity, (image rows), PageEnd
    // NOTE: PrintEnd is NOT in the packet list — it's sent after polling PrintStatus.
    REQUIRE(job.packets.size() >= 8);
    REQUIRE(job.total_rows == 4);

    // First packet: SetDensity
    REQUIRE(job.packets[0][2] == static_cast<uint8_t>(NiimbotCmd::SetDensity));

    // Second: SetLabelType
    REQUIRE(job.packets[1][2] == static_cast<uint8_t>(NiimbotCmd::SetLabelType));

    // Third: PrintStart
    REQUIRE(job.packets[2][2] == static_cast<uint8_t>(NiimbotCmd::PrintStart));

    // Fourth: PrintClear (required by D110)
    REQUIRE(job.packets[3][2] == static_cast<uint8_t>(NiimbotCmd::PrintClear));

    // Fifth: PageStart
    REQUIRE(job.packets[4][2] == static_cast<uint8_t>(NiimbotCmd::PageStart));

    // Sixth: SetPageSize
    REQUIRE(job.packets[5][2] == static_cast<uint8_t>(NiimbotCmd::SetPageSize));

    // Seventh: SetQuantity
    REQUIRE(job.packets[6][2] == static_cast<uint8_t>(NiimbotCmd::SetQuantity));

    // Last packet: PageEnd (PrintEnd sent separately after status polling)
    size_t n = job.packets.size();
    REQUIRE(job.packets[n - 1][2] == static_cast<uint8_t>(NiimbotCmd::PageEnd));
}

TEST_CASE("niimbot_build_print_job - blank rows use PrintEmptyRow", "[niimbot][protocol]") {
    // All-white bitmap (default-initialized to 0x00)
    LabelBitmap bmp(8, 10);

    LabelSize size{"15x30mm", 96, 231, 203, 0x01, 15, 30};

    auto job = niimbot_build_print_job(bmp, size);

    // Image rows should be a single PrintEmptyRow with [row_hi, row_lo, repeat_count]
    bool found_empty = false;
    for (const auto& pkt : job.packets) {
        if (pkt[2] == static_cast<uint8_t>(NiimbotCmd::PrintEmptyRow)) {
            found_empty = true;
            // Payload: 3 bytes [row_hi=0x00, row_lo=0x00, count=10]
            REQUIRE(pkt[3] == 0x03); // length = 3
            REQUIRE(pkt[4] == 0x00); // row high byte
            REQUIRE(pkt[5] == 0x00); // row low byte (starting at row 0)
            REQUIRE(pkt[6] == 0x0A); // repeat count = 10
        }
    }
    REQUIRE(found_empty);
}

// ============================================================================
// Label sizes
// ============================================================================

TEST_CASE("niimbot_b21_sizes - returns valid sizes", "[niimbot][protocol]") {
    auto sizes = niimbot_b21_sizes();
    REQUIRE(!sizes.empty());
    for (const auto& s : sizes) {
        REQUIRE(s.dpi == 203);
        REQUIRE(s.width_px > 0);
        REQUIRE(s.height_px > 0);
    }
}

TEST_CASE("niimbot_d11_sizes - returns valid sizes", "[niimbot][protocol]") {
    auto sizes = niimbot_d11_sizes();
    REQUIRE(!sizes.empty());
    for (const auto& s : sizes) {
        REQUIRE(s.dpi == 203);
        REQUIRE(s.width_px == 96); // D11 printhead
    }
}
