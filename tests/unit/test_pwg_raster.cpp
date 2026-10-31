// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "label_bitmap.h"
#include "pwg_raster.h"

#include "../catch_amalgamated.hpp"

using helix::LabelBitmap;
using namespace helix::pwg;

// Helper to read big-endian uint32 from a buffer
static uint32_t read_be32(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

// ---------------------------------------------------------------------------
// PackBits encoding
// ---------------------------------------------------------------------------

// Decodes one PWG Raster scanline (PWG 5102.4 / CUPS raster compression, 1-byte pixels):
// 0..127 repeats the next pixel count+1 times, 129..255 copies 257-count literal pixels.
// 128 is not a valid PWG code; it fails the decode.
static bool pwg_decode_line(const std::vector<uint8_t>& in, std::vector<uint8_t>& out) {
    size_t i = 0;
    while (i < in.size()) {
        uint8_t code = in[i++];
        if (code == 128)
            return false;
        if (code < 128) {
            if (i >= in.size())
                return false;
            out.insert(out.end(), static_cast<size_t>(code) + 1, in[i++]);
        } else {
            size_t n = 257 - static_cast<size_t>(code);
            if (i + n > in.size())
                return false;
            out.insert(out.end(), in.begin() + i, in.begin() + i + n);
            i += n;
        }
    }
    return true;
}

TEST_CASE("PackBits encoding - literal bytes", "[label][pwg]") {
    // Distinct bytes: should encode as literals
    std::vector<uint8_t> input = {0x01, 0x02, 0x03, 0x04};
    std::vector<uint8_t> out;
    packbits_encode(input.data(), input.size(), out);

    // Literal encoding: (257 - count) followed by the bytes
    REQUIRE(out.size() == 5);
    REQUIRE(out[0] == 253); // 257 - 4
    REQUIRE(out[1] == 0x01);
    REQUIRE(out[2] == 0x02);
    REQUIRE(out[3] == 0x03);
    REQUIRE(out[4] == 0x04);
}

TEST_CASE("PackBits encoding - single literal is a run of one", "[label][pwg]") {
    // 257 - 1 does not fit a byte; one pixel is a run of length 1
    std::vector<uint8_t> input = {0x42};
    std::vector<uint8_t> out;
    packbits_encode(input.data(), input.size(), out);

    REQUIRE(out.size() == 2);
    REQUIRE(out[0] == 0);
    REQUIRE(out[1] == 0x42);
}

TEST_CASE("PackBits encoding - round-trips through a PWG decoder", "[label][pwg]") {
    std::vector<uint8_t> input;
    for (int i = 0; i < 300; i++)
        input.push_back(static_cast<uint8_t>(i * 7)); // 300 distinct-neighbor bytes
    input.insert(input.end(), 200, 0xFF);             // a run longer than 128
    input.push_back(0x00);                            // one trailing literal
    input.insert(input.end(), {0x10, 0x20, 0x20, 0x30});

    std::vector<uint8_t> encoded;
    packbits_encode(input.data(), input.size(), encoded);

    std::vector<uint8_t> decoded;
    REQUIRE(pwg_decode_line(encoded, decoded));
    REQUIRE(decoded == input);
}

TEST_CASE("PackBits encoding - run of identical bytes", "[label][pwg]") {
    // 10 identical bytes: should encode as a single run
    std::vector<uint8_t> input(10, 0xAA);
    std::vector<uint8_t> out;
    packbits_encode(input.data(), input.size(), out);

    // Run encoding: (count-1) followed by the byte
    // count=10 -> 10-1=9
    REQUIRE(out.size() == 2);
    REQUIRE(out[0] == 9); // 10 - 1
    REQUIRE(out[1] == 0xAA);
}

TEST_CASE("PackBits encoding - mixed content", "[label][pwg]") {
    // 3 different bytes followed by 5 identical bytes
    std::vector<uint8_t> input = {0x01, 0x02, 0x03, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    std::vector<uint8_t> out;
    packbits_encode(input.data(), input.size(), out);

    // Should produce: literal(3 bytes) + run(5 of 0xFF)
    REQUIRE(out.size() >= 2); // At minimum some output

    // First chunk: 3 literals -> (257-3=254), 0x01, 0x02, 0x03
    REQUIRE(out[0] == 254);
    REQUIRE(out[1] == 0x01);
    REQUIRE(out[2] == 0x02);
    REQUIRE(out[3] == 0x03);

    // Second chunk: run of 5 -> (5-1=4), 0xFF
    REQUIRE(out[4] == 4);
    REQUIRE(out[5] == 0xFF);
    REQUIRE(out.size() == 6);
}

TEST_CASE("PackBits encoding - all same byte", "[label][pwg]") {
    // 128 identical bytes (max run length)
    std::vector<uint8_t> input(128, 0x55);
    std::vector<uint8_t> out;
    packbits_encode(input.data(), input.size(), out);

    // Should compress to just 2 bytes: (128-1=127), 0x55
    REQUIRE(out.size() == 2);
    REQUIRE(out[0] == 127);
    REQUIRE(out[1] == 0x55);
}

TEST_CASE("PackBits encoding - empty input", "[label][pwg]") {
    std::vector<uint8_t> out;
    packbits_encode(nullptr, 0, out);
    REQUIRE(out.empty());
}

// ---------------------------------------------------------------------------
// Page header
// ---------------------------------------------------------------------------

TEST_CASE("Page header size", "[label][pwg]") {
    PageParams params;
    params.width_px = 100;
    params.height_px = 200;
    auto hdr = build_page_header(params);
    REQUIRE(hdr.size() == 1796);
}

TEST_CASE("Page header - resolution", "[label][pwg]") {
    PageParams params;
    params.hw_resolution_x = 300;
    params.hw_resolution_y = 600;
    params.width_px = 100;
    params.height_px = 200;

    auto hdr = build_page_header(params);

    // HWResolution at offsets 276 and 280
    REQUIRE(read_be32(hdr.data() + 276) == 300);
    REQUIRE(read_be32(hdr.data() + 280) == 600);
}

// Offsets are PWG 5102.4 section 4.3 (the CUPS cups_page_header2_t layout).
TEST_CASE("Page header - every field at its PWG 5102.4 offset", "[label][pwg]") {
    PageParams params;
    params.hw_resolution_x = 300;
    params.hw_resolution_y = 300;
    params.width_px = 2480; // A4 at 300 dpi
    params.height_px = 3508;
    params.color_space = ColorSpace::SGRAY;
    params.bits_per_color = 8;
    params.bits_per_pixel = 8;
    params.num_colors = 1;
    params.page_size_name = "iso_a4_210x297mm";

    auto hdr = build_page_header(params);
    const uint8_t* h = hdr.data();

    REQUIRE(std::string(reinterpret_cast<const char*>(h + 0)) == "PwgRaster");    // MediaClass
    REQUIRE(std::string(reinterpret_cast<const char*>(h + 128)) == "stationery"); // MediaType
    REQUIRE(read_be32(h + 276) == 300);                                           // HWResolution
    REQUIRE(read_be32(h + 280) == 300);
    REQUIRE(read_be32(h + 340) == 1);    // NumCopies
    REQUIRE(read_be32(h + 352) == 595);  // PageSize[0], points
    REQUIRE(read_be32(h + 356) == 842);  // PageSize[1], points
    REQUIRE(read_be32(h + 368) == 0);    // Tumble
    REQUIRE(read_be32(h + 372) == 2480); // Width
    REQUIRE(read_be32(h + 376) == 3508); // Height
    REQUIRE(read_be32(h + 380) == 0);    // cupsMediaType
    REQUIRE(read_be32(h + 384) == 8);    // BitsPerColor
    REQUIRE(read_be32(h + 388) == 8);    // BitsPerPixel
    REQUIRE(read_be32(h + 392) == 2480); // BytesPerLine
    REQUIRE(read_be32(h + 396) == 0);    // ColorOrder: chunky
    REQUIRE(read_be32(h + 400) == static_cast<uint32_t>(ColorSpace::SGRAY));
    REQUIRE(read_be32(h + 404) == 0); // cupsCompression
    REQUIRE(read_be32(h + 420) == 1); // NumColors
    REQUIRE(read_be32(h + 452) == 1); // TotalPageCount
    REQUIRE(read_be32(h + 456) == 1); // CrossFeedTransform
    REQUIRE(read_be32(h + 460) == 1); // FeedTransform
    REQUIRE(std::string(reinterpret_cast<const char*>(h + 1732)) == "iso_a4_210x297mm");
}

TEST_CASE("Page header - Letter page size in points", "[label][pwg]") {
    PageParams params;
    params.hw_resolution_x = 300;
    params.hw_resolution_y = 600;
    params.width_px = 2550;
    params.height_px = 6600;

    auto hdr = build_page_header(params);

    REQUIRE(read_be32(hdr.data() + 352) == 612);
    REQUIRE(read_be32(hdr.data() + 356) == 792);
}

TEST_CASE("Page header - sRGB fields", "[label][pwg]") {
    PageParams params;
    params.width_px = 100;
    params.height_px = 100;
    params.color_space = ColorSpace::SRGB;
    params.bits_per_pixel = 24;
    params.num_colors = 3;

    auto hdr = build_page_header(params);

    REQUIRE(read_be32(hdr.data() + 388) == 24);
    REQUIRE(read_be32(hdr.data() + 392) == 300);
    REQUIRE(read_be32(hdr.data() + 400) == static_cast<uint32_t>(ColorSpace::SRGB));
    REQUIRE(read_be32(hdr.data() + 420) == 3);
}

// ---------------------------------------------------------------------------
// Full generation
// ---------------------------------------------------------------------------

TEST_CASE("Page header - sync word", "[label][pwg]") {
    LabelBitmap bmp(8, 8);
    bmp.fill(false); // white

    PageParams params;
    auto out = generate(bmp, params);

    // First 4 bytes are the "RaS2" sync word
    REQUIRE(out.size() >= 4);
    REQUIRE(out[0] == 'R');
    REQUIRE(out[1] == 'a');
    REQUIRE(out[2] == 'S');
    REQUIRE(out[3] == '2');
}

TEST_CASE("Generate - white bitmap", "[label][pwg]") {
    // All-white 100x100 bitmap at 8bpp should compress very well
    LabelBitmap bmp(100, 100);
    bmp.fill(false); // white

    PageParams params;
    params.width_px = 100;
    params.height_px = 100;

    auto out = generate(bmp, params);

    // Output should be much smaller than uncompressed (100*100 = 10000 bytes)
    // Sync(4) + header(1796) + compressed data
    REQUIRE(out.size() > 4 + 1796);
    size_t data_size = out.size() - 4 - 1796;
    // White rows are all 0xFF, which compress to a short run.
    // Repeated lines further compress via repeat count.
    REQUIRE(data_size < 1000);
}

TEST_CASE("Generate - black bitmap", "[label][pwg]") {
    // All-black bitmap: 1bpp black(1) maps to 8bpp 0x00
    LabelBitmap bmp(16, 4);
    bmp.fill(true); // all black

    PageParams params;
    params.width_px = 16;
    params.height_px = 4;

    auto out = generate(bmp, params);
    REQUIRE(out.size() > 4 + 1796);

    // After sync + header, the page data starts.
    // First byte of page data is the repeat count for the first unique line.
    size_t data_start = 4 + 1796;
    // The repeat count for 4 identical all-black lines: first line + 3 repeats
    uint8_t repeat_count = out[data_start];
    REQUIRE(repeat_count == 3); // 4 identical rows -> first emitted + 3 repeats

    // The PackBits-encoded line follows: run of 16 bytes of 0x00
    // run encoding: (16-1=15), 0x00
    REQUIRE(out[data_start + 1] == 15);
    REQUIRE(out[data_start + 2] == 0x00);
}

TEST_CASE("Generate - small bitmap", "[label][pwg]") {
    // Verify complete output structure: sync + header + page data
    LabelBitmap bmp(8, 2);
    bmp.set_pixel(0, 0, true); // one black pixel at top-left

    PageParams params;
    params.width_px = 8;
    params.height_px = 2;

    auto out = generate(bmp, params);

    // Sync word
    REQUIRE(out[0] == 'R');
    REQUIRE(out[1] == 'a');
    REQUIRE(out[2] == 'S');
    REQUIRE(out[3] == '2');

    // Header size
    REQUIRE(out.size() > 4 + 1796);

    // Dimensions in header
    REQUIRE(read_be32(out.data() + 4 + 372) == 8);
    REQUIRE(read_be32(out.data() + 4 + 376) == 2);
}

TEST_CASE("Generate - 1bpp to 8bpp conversion", "[label][pwg]") {
    // Single row, 8 pixels: alternating black/white
    LabelBitmap bmp(8, 1);
    bmp.set_pixel(0, 0, true);  // black -> 0x00
    bmp.set_pixel(1, 0, false); // white -> 0xFF
    bmp.set_pixel(2, 0, true);  // black -> 0x00
    bmp.set_pixel(3, 0, false); // white -> 0xFF
    bmp.set_pixel(4, 0, true);  // black
    bmp.set_pixel(5, 0, false); // white
    bmp.set_pixel(6, 0, true);  // black
    bmp.set_pixel(7, 0, false); // white

    PageParams params;
    params.width_px = 8;
    params.height_px = 1;

    auto out = generate(bmp, params);

    // After sync(4) + header(1796), page data starts.
    // First byte: repeat count (0 for single line)
    size_t data_start = 4 + 1796;
    REQUIRE(out[data_start] == 0); // no repeats (only 1 line)

    // The 8-byte scanline 0x00, 0xFF, 0x00, 0xFF, ... has no neighbor runs,
    // so it is one literal chunk: (257-8=249), then 8 bytes
    REQUIRE(out[data_start + 1] == 249); // 257 - 8
    REQUIRE(out[data_start + 2] == 0x00);
    REQUIRE(out[data_start + 3] == 0xFF);
    REQUIRE(out[data_start + 4] == 0x00);
    REQUIRE(out[data_start + 5] == 0xFF);
}

// ---------------------------------------------------------------------------
// Multi-page generation
// ---------------------------------------------------------------------------

TEST_CASE("Generate multi - empty", "[label][pwg]") {
    std::vector<const LabelBitmap*> pages;
    PageParams params;
    auto out = generate_multi(pages, params);
    // Empty page list returns empty output (no sync word either)
    REQUIRE(out.empty());
}

TEST_CASE("Generate multi - single page", "[label][pwg]") {
    LabelBitmap bmp(8, 4);
    bmp.fill(false);

    PageParams params;
    params.width_px = 8;
    params.height_px = 4;

    std::vector<const LabelBitmap*> pages = {&bmp};
    auto multi_out = generate_multi(pages, params);
    auto single_out = generate(bmp, params);

    // Single-page multi should produce identical output to generate()
    REQUIRE(multi_out.size() == single_out.size());
    REQUIRE(multi_out == single_out);
}

TEST_CASE("Generate multi - TotalPageCount counts the pages", "[label][pwg]") {
    LabelBitmap a(8, 2);
    LabelBitmap b(8, 2);
    a.fill(false);
    b.fill(false);

    PageParams params;
    params.width_px = 8;
    params.height_px = 2;

    std::vector<const LabelBitmap*> pages = {&a, nullptr, &b};
    auto out = generate_multi(pages, params);

    REQUIRE(read_be32(out.data() + 4 + 452) == 2);
}
