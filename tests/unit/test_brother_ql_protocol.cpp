// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "brother_ql_protocol.h"

#include "../catch_amalgamated.hpp"

using namespace helix;
using namespace helix::label;

TEST_CASE("Brother QL protocol - invalidation header", "[label][brother]") {
    LabelBitmap bitmap(696, 200);
    LabelSize size{"62mm", 696, 0, 300, 0x0A, 62, 0};

    auto data = brother_ql_build_raster(bitmap, size);

    // First 200 bytes must be 0x00 (invalidation)
    REQUIRE(data.size() > 200);
    for (int i = 0; i < 200; i++) {
        REQUIRE(data[i] == 0x00);
    }
}

TEST_CASE("Brother QL protocol - init sequence", "[label][brother]") {
    LabelBitmap bitmap(696, 200);
    LabelSize size{"62mm", 696, 0, 300, 0x0A, 62, 0};

    auto data = brother_ql_build_raster(bitmap, size);

    // After 200 bytes of invalidation: ESC @ (initialize)
    REQUIRE(data[200] == 0x1B);
    REQUIRE(data[201] == 0x40);
}

TEST_CASE("Brother QL protocol - raster mode command", "[label][brother]") {
    LabelBitmap bitmap(696, 200);
    LabelSize size{"62mm", 696, 0, 300, 0x0A, 62, 0};

    auto data = brother_ql_build_raster(bitmap, size);

    // ESC i a 01 (raster mode) follows ESC @
    REQUIRE(data[202] == 0x1B);
    REQUIRE(data[203] == 0x69);
    REQUIRE(data[204] == 0x61);
    REQUIRE(data[205] == 0x01);
}

TEST_CASE("Brother QL protocol - media info continuous", "[label][brother]") {
    LabelBitmap bitmap(696, 200);
    LabelSize size{"62mm", 696, 0, 300, 0x0A, 62, 0};

    auto data = brother_ql_build_raster(bitmap, size);

    // ESC i z at offset 206
    REQUIRE(data[206] == 0x1B);
    REQUIRE(data[207] == 0x69);
    REQUIRE(data[208] == 0x7A);
    // Continuous: PI + width + media_type = 0x80 | 0x04 | 0x02 = 0x86
    REQUIRE(data[209] == 0x86);
    REQUIRE(data[210] == 0x0A); // media type (continuous)
    REQUIRE(data[211] == 62);   // width mm
    REQUIRE(data[212] == 0);    // length mm (continuous = 0)
}

TEST_CASE("Brother QL protocol - media info die-cut", "[label][brother]") {
    LabelBitmap bitmap(306, 991);
    LabelSize size{"29x90mm", 306, 991, 300, 0x0B, 29, 90};

    auto data = brother_ql_build_raster(bitmap, size);

    // ESC i z at offset 206
    REQUIRE(data[209] == 0x8E); // Die-cut: PI + width + media_type + length
    REQUIRE(data[210] == 0x0B); // media type (die-cut)
    REQUIRE(data[211] == 29);   // width mm
    REQUIRE(data[212] == 90);   // length mm
}

TEST_CASE("Brother QL protocol - print command at end", "[label][brother]") {
    LabelBitmap bitmap(696, 200);
    LabelSize size{"62mm", 696, 0, 300, 0x0A, 62, 0};

    auto data = brother_ql_build_raster(bitmap, size);

    // Last byte is 0x1A (print command)
    REQUIRE(data.back() == 0x1A);
}

TEST_CASE("Brother QL protocol - horizontal flip verification", "[label][brother]") {
    LabelBitmap bitmap(696, 1);
    LabelSize size{"62mm", 696, 0, 300, 0x0A, 62, 0};

    // Set pixel at x=0, y=0 (leftmost)
    bitmap.set_pixel(0, 0, true);

    auto data = brother_ql_build_raster(bitmap, size);

    // Find the raster data row (0x67 0x00 marker)
    bool found_raster = false;
    size_t raster_start = 0;
    for (size_t i = 0; i + 2 < data.size(); i++) {
        if (data[i] == 0x67 && data[i + 1] == 0x00 &&
            data[i + 2] == static_cast<uint8_t>(BROTHER_QL_RASTER_ROW_BYTES)) {
            found_raster = true;
            raster_start = i + 3; // skip header, data starts here
            break;
        }
    }
    REQUIRE(found_raster);

    // Data is left-justified (no left padding for continuous labels)
    // After horizontal flip, pixel at x=0 should appear at x=695 (width-1)
    // x=695 is in byte 695/8 = 86, bit 7-(695%8) = 7-7 = 0
    size_t pixel_byte_offset = raster_start + 86;
    REQUIRE(pixel_byte_offset < data.size());
    // Bit 0 should be set (flipped pixel)
    REQUIRE((data[pixel_byte_offset] & 0x01) != 0);

    // Original position x=0 should NOT be set (pixel was flipped away)
    size_t orig_byte_offset = raster_start;
    // bit 7 should NOT be set
    REQUIRE((data[orig_byte_offset] & 0x80) == 0);
}

TEST_CASE("Brother QL protocol - 38mm narrow label right-justified with flip", "[label][brother]") {
    LabelBitmap bitmap(413, 1);
    LabelSize size{"38mm", 413, 0, 300, 0x0A, 38, 0};

    // Set pixel at x=0 (leftmost pixel of 38mm label)
    bitmap.set_pixel(0, 0, true);

    auto data = brother_ql_build_raster(bitmap, size);

    // Find raster row
    bool found_raster = false;
    size_t raster_start = 0;
    for (size_t i = 0; i + 2 < data.size(); i++) {
        if (data[i] == 0x67 && data[i + 1] == 0x00 &&
            data[i + 2] == static_cast<uint8_t>(BROTHER_QL_RASTER_ROW_BYTES)) {
            found_raster = true;
            raster_start = i + 3;
            break;
        }
    }
    REQUIRE(found_raster);

    int label_byte_width = (413 + 7) / 8;                           // 52
    int right_pad = BROTHER_QL_RASTER_ROW_BYTES - label_byte_width; // 38

    // Data is left-justified, right padding bytes should be zero
    for (int i = 0; i < right_pad; i++) {
        REQUIRE(data[raster_start + label_byte_width + i] == 0x00);
    }

    // After flip, pixel at x=0 should appear at x=412 (width-1)
    // x=412 is in byte 412/8=51, bit 7-(412%8) = 7-4 = 3
    REQUIRE((data[raster_start + 51] & (1 << 3)) != 0);

    // Original position x=0 (byte 0, bit 7) should NOT be set
    REQUIRE((data[raster_start] & 0x80) == 0);
}

TEST_CASE("Brother QL protocol - empty bitmap", "[label][brother]") {
    LabelBitmap bitmap(696, 0);
    LabelSize size{"62mm", 696, 0, 300, 0x0A, 62, 0};

    auto data = brother_ql_build_raster(bitmap, size);

    // Should still have header + print command, just no raster rows
    REQUIRE(data.size() > 200);
    REQUIRE(data.back() == 0x1A);
}

TEST_CASE("Brother QL protocol - deterministic output", "[label][brother]") {
    LabelBitmap bitmap(696, 100);
    LabelSize size{"62mm", 696, 0, 300, 0x0A, 62, 0};

    auto data1 = brother_ql_build_raster(bitmap, size);
    auto data2 = brother_ql_build_raster(bitmap, size);

    REQUIRE(data1 == data2);
}
