// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "phomemo_printer.h"

#include <filesystem>
#include <fstream>
#include <string>

#include "../catch_amalgamated.hpp"

TEST_CASE("PhomemoPrinter::build_raster_commands header has speed, density, media type",
          "[label]") {
    auto bitmap = helix::LabelBitmap::create(319, 10, 203);
    auto sizes = helix::PhomemoPrinter::supported_sizes_static();
    auto& size = sizes[0]; // 40x30mm

    auto commands = helix::PhomemoPrinter::build_raster_commands(bitmap, size);

    // Header (11 bytes):
    //   1B 4E 0D <speed>       — print speed
    //   1B 4E 04 <density>     — print density
    //   1F 11 <media_type>     — media type
    REQUIRE(commands.size() > 11);

    // Speed: ESC N 0D 03
    REQUIRE(commands[0] == 0x1B);
    REQUIRE(commands[1] == 0x4E);
    REQUIRE(commands[2] == 0x0D);
    REQUIRE(commands[3] == 0x03); // default speed

    // Density: ESC N 04 08
    REQUIRE(commands[4] == 0x1B);
    REQUIRE(commands[5] == 0x4E);
    REQUIRE(commands[6] == 0x04);
    REQUIRE(commands[7] == 0x0A); // default density

    // Media type: 1F 11 0A
    REQUIRE(commands[8] == 0x1F);
    REQUIRE(commands[9] == 0x11);
    REQUIRE(commands[10] == 0x0A); // gap labels
}

TEST_CASE("PhomemoPrinter::build_raster_commands GS v 0 raster header", "[label]") {
    auto bitmap = helix::LabelBitmap::create(319, 10, 203);
    auto sizes = helix::PhomemoPrinter::supported_sizes_static();
    auto& size = sizes[0]; // 40x30mm: 319px wide = 40 bytes per line

    auto commands = helix::PhomemoPrinter::build_raster_commands(bitmap, size);

    // GS v 0 at offset 11 (right after header)
    REQUIRE(commands[11] == 0x1D); // GS
    REQUIRE(commands[12] == 0x76); // v
    REQUIRE(commands[13] == 0x30); // 0
    REQUIRE(commands[14] == 0x00); // normal mode

    // bytes_per_line = (319 + 7) / 8 = 40, 16-bit LE
    REQUIRE(commands[15] == 40); // lo
    REQUIRE(commands[16] == 0);  // hi

    // num_lines = 10, 16-bit LE
    REQUIRE(commands[17] == 10); // lo
    REQUIRE(commands[18] == 0);  // hi
}

TEST_CASE("PhomemoPrinter::build_raster_commands total size", "[label]") {
    int width = 319;
    int height = 15;
    auto bitmap = helix::LabelBitmap::create(width, height, 203);
    auto sizes = helix::PhomemoPrinter::supported_sizes_static();
    auto& size = sizes[0]; // 40x30mm

    auto commands = helix::PhomemoPrinter::build_raster_commands(bitmap, size);

    int bytes_per_line = (width + 7) / 8;                            // 40
    size_t expected = 11                                             // header
                      + 8                                            // GS v 0 command + dimensions
                      + static_cast<size_t>(bytes_per_line) * height // raster data
                      + 8;                                           // footer
    REQUIRE(commands.size() == expected);
}

TEST_CASE("PhomemoPrinter::build_raster_commands footer", "[label]") {
    auto bitmap = helix::LabelBitmap::create(319, 5, 203);
    auto sizes = helix::PhomemoPrinter::supported_sizes_static();
    auto& size = sizes[0]; // 40x30mm

    auto commands = helix::PhomemoPrinter::build_raster_commands(bitmap, size);

    size_t n = commands.size();
    // Footer (last 8 bytes):
    //   1F F0 05 00 — finalize
    //   1F F0 03 00 — feed to gap
    REQUIRE(commands[n - 8] == 0x1F);
    REQUIRE(commands[n - 7] == 0xF0);
    REQUIRE(commands[n - 6] == 0x05);
    REQUIRE(commands[n - 5] == 0x00);

    REQUIRE(commands[n - 4] == 0x1F);
    REQUIRE(commands[n - 3] == 0xF0);
    REQUIRE(commands[n - 2] == 0x03);
    REQUIRE(commands[n - 1] == 0x00);
}

TEST_CASE("PhomemoPrinter::supported_sizes all 203 DPI", "[label]") {
    auto sizes = helix::PhomemoPrinter::supported_sizes_static();
    REQUIRE(sizes.size() == 8);

    for (auto& s : sizes) {
        REQUIRE(s.dpi == 203);
    }

    // Verify first size: 40x30mm (319x240px)
    REQUIRE(sizes[0].name == "40x30mm");
    REQUIRE(sizes[0].width_px == 319);
    REQUIRE(sizes[0].height_px == 240);
    REQUIRE(sizes[0].width_mm == 40);
    REQUIRE(sizes[0].length_mm == 30);
    REQUIRE(sizes[0].media_type == 0x0A);
}

TEST_CASE("PhomemoPrinter::build_raster_commands known pixel pattern", "[label]") {
    // 8px wide = 1 byte per row, 1 row tall
    auto bitmap = helix::LabelBitmap::create(8, 1, 203);
    // Set pixels: x=0 and x=7 (MSB and LSB of byte)
    bitmap.set_pixel(0, 0, true);
    bitmap.set_pixel(7, 0, true);
    // Expected byte: 0x81 (10000001)

    // Use 25x10mm size (200x80px) — width won't matter for an 8px bitmap
    // but we need a valid size. The bitmap bytes are copied directly.
    helix::LabelSize small_size{"test", 8, 1, 203, 0x0A, 25, 10};
    auto commands = helix::PhomemoPrinter::build_raster_commands(bitmap, small_size);

    // Raster data starts at offset 19 (11 header + 8 GS v 0)
    // bytes_per_line = 1, num_lines = 1
    REQUIRE(commands[15] == 1); // bytes_per_line lo
    REQUIRE(commands[17] == 1); // num_lines lo

    // The single raster byte: no flip needed for Phomemo
    REQUIRE(commands[19] == 0x81);
}

TEST_CASE("PhomemoPrinter::build_raster_commands continuous media type", "[label]") {
    auto bitmap = helix::LabelBitmap::create(319, 5, 203);
    // Continuous media uses 0x0B
    helix::LabelSize continuous{"continuous", 319, 0, 203, 0x0B, 40, 0};

    auto commands = helix::PhomemoPrinter::build_raster_commands(bitmap, continuous);

    // Media type byte at offset 10
    REQUIRE(commands[10] == 0x0B);
}

TEST_CASE("PhomemoPrinter::name returns correct name", "[label]") {
    helix::PhomemoPrinter printer;
    REQUIRE(printer.name() == "Phomemo M110");
}

TEST_CASE("PhomemoPrinter::build_raster_commands wider label", "[label]") {
    // 50x30mm: 400px wide = 50 bytes per line
    auto bitmap = helix::LabelBitmap::create(400, 3, 203);
    bitmap.set_pixel(0, 0, true);

    auto sizes = helix::PhomemoPrinter::supported_sizes_static();
    // Find 50x30mm
    helix::LabelSize size_50;
    for (auto& s : sizes) {
        if (s.width_mm == 50 && s.length_mm == 30) {
            size_50 = s;
            break;
        }
    }

    auto commands = helix::PhomemoPrinter::build_raster_commands(bitmap, size_50);

    // bytes_per_line = 50
    REQUIRE(commands[15] == 50); // lo
    REQUIRE(commands[16] == 0);  // hi
    // num_lines = 3
    REQUIRE(commands[17] == 3); // lo
    REQUIRE(commands[18] == 0); // hi
}

TEST_CASE("PhomemoPrinter::read_sysfs_usb_id never throws on a bad sysfs value",
          "[label][usb-detect]") {
    // A device unplugged mid-read leaves idVendor empty; the reader runs on a
    // detached print thread, where a throw is std::terminate.
    const auto dir = std::filesystem::temp_directory_path() / "helix_phomemo_sysfs";
    std::filesystem::create_directories(dir);
    auto write = [&](const std::string& name, const std::string& content) {
        std::ofstream(dir / name) << content;
        return (dir / name).string();
    };

    CHECK(helix::PhomemoPrinter::read_sysfs_usb_id(write("ok", "0493\n")) == 0x0493);
    CHECK(helix::PhomemoPrinter::read_sysfs_usb_id((dir / "missing").string()) == 0);
    CHECK(helix::PhomemoPrinter::read_sysfs_usb_id(write("empty", "")) == 0);
    CHECK(helix::PhomemoPrinter::read_sysfs_usb_id(write("junk", "zz\n")) == 0);
    CHECK(helix::PhomemoPrinter::read_sysfs_usb_id(write("wide", "12345\n")) == 0);

    std::filesystem::remove_all(dir);
}
