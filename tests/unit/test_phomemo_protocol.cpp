// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "phomemo_protocol.h"

#include "../catch_amalgamated.hpp"

using namespace helix;
using namespace helix::label;

TEST_CASE("Phomemo protocol - speed command", "[label][phomemo]") {
    LabelBitmap bitmap(319, 240);
    LabelSize size{"40x30mm", 319, 240, 203, 0x0A, 40, 30};

    auto data = phomemo_build_raster(bitmap, size);

    // First 4 bytes: ESC N 0D <speed>
    REQUIRE(data.size() >= 4);
    REQUIRE(data[0] == 0x1B);
    REQUIRE(data[1] == 0x4E);
    REQUIRE(data[2] == 0x0D);
    REQUIRE(data[3] == PHOMEMO_DEFAULT_SPEED);
}

TEST_CASE("Phomemo protocol - density command", "[label][phomemo]") {
    LabelBitmap bitmap(319, 240);
    LabelSize size{"40x30mm", 319, 240, 203, 0x0A, 40, 30};

    auto data = phomemo_build_raster(bitmap, size);

    // Bytes 4-7: ESC N 04 <density>
    REQUIRE(data.size() >= 8);
    REQUIRE(data[4] == 0x1B);
    REQUIRE(data[5] == 0x4E);
    REQUIRE(data[6] == 0x04);
    REQUIRE(data[7] == PHOMEMO_DEFAULT_DENSITY);
}

TEST_CASE("Phomemo protocol - GS v 0 raster command", "[label][phomemo]") {
    LabelBitmap bitmap(319, 240);
    LabelSize size{"40x30mm", 319, 240, 203, 0x0A, 40, 30};

    auto data = phomemo_build_raster(bitmap, size);

    // Must contain GS v 0 command (0x1D 0x76 0x30)
    bool found = false;
    for (size_t i = 0; i + 2 < data.size(); i++) {
        if (data[i] == 0x1D && data[i + 1] == 0x76 && data[i + 2] == 0x30) {
            found = true;
            break;
        }
    }
    REQUIRE(found);
}

TEST_CASE("Phomemo protocol - finalize and feed-to-gap", "[label][phomemo]") {
    LabelBitmap bitmap(319, 240);
    LabelSize size{"40x30mm", 319, 240, 203, 0x0A, 40, 30};

    auto data = phomemo_build_raster(bitmap, size);

    // Last 8 bytes: finalize (1F F0 05 00) + feed-to-gap (1F F0 03 00)
    REQUIRE(data.size() >= 8);
    size_t n = data.size();

    // Finalize
    REQUIRE(data[n - 8] == 0x1F);
    REQUIRE(data[n - 7] == 0xF0);
    REQUIRE(data[n - 6] == 0x05);
    REQUIRE(data[n - 5] == 0x00);

    // Feed to gap
    REQUIRE(data[n - 4] == 0x1F);
    REQUIRE(data[n - 3] == 0xF0);
    REQUIRE(data[n - 2] == 0x03);
    REQUIRE(data[n - 1] == 0x00);
}

TEST_CASE("Phomemo protocol - empty bitmap", "[label][phomemo]") {
    LabelBitmap bitmap(319, 0);
    LabelSize size{"40x30mm", 319, 0, 203, 0x0A, 40, 30};

    auto data = phomemo_build_raster(bitmap, size);

    // Should still have header + GS v 0 + footer, no raster data
    REQUIRE(data.size() >= 19); // 11 header + 8 footer
    // Feed to gap at end
    size_t n = data.size();
    REQUIRE(data[n - 4] == 0x1F);
    REQUIRE(data[n - 3] == 0xF0);
    REQUIRE(data[n - 2] == 0x03);
    REQUIRE(data[n - 1] == 0x00);
}

TEST_CASE("Phomemo protocol - deterministic output", "[label][phomemo]") {
    LabelBitmap bitmap(319, 240);
    LabelSize size{"40x30mm", 319, 240, 203, 0x0A, 40, 30};

    auto data1 = phomemo_build_raster(bitmap, size);
    auto data2 = phomemo_build_raster(bitmap, size);

    REQUIRE(data1 == data2);
}
