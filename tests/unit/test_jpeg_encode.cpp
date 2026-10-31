// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_jpeg_encode.cpp
 * @brief The screen server's JPEG path: pixel conversion to RGB888, then encode.
 *
 * Decoded with stb_image, so the round trip checks real image content rather than
 * the encoder's own bookkeeping.
 */

#include "jpeg_encode.h"
#include "stb_image.h"

#include <cstdint>
#include <cstdlib>
#include <vector>

#include "../catch_amalgamated.hpp"

using namespace helix;

namespace {

constexpr int kW = 64;
constexpr int kH = 48;

struct Rgb {
    uint8_t r, g, b;
};

// Red, green, blue, white quadrants, row-major from the top-left.
Rgb quadrant_color(int x, int y) {
    const bool right = x >= kW / 2;
    const bool bottom = y >= kH / 2;
    if (!right && !bottom) {
        return {255, 0, 0};
    }
    if (right && !bottom) {
        return {0, 255, 0};
    }
    if (!right) {
        return {0, 0, 255};
    }
    return {255, 255, 255};
}

std::vector<uint8_t> quadrant_rgb() {
    std::vector<uint8_t> rgb(static_cast<size_t>(kW) * kH * 3);
    for (int y = 0; y < kH; ++y) {
        for (int x = 0; x < kW; ++x) {
            const Rgb c = quadrant_color(x, y);
            uint8_t* p = &rgb[(static_cast<size_t>(y) * kW + x) * 3];
            p[0] = c.r;
            p[1] = c.g;
            p[2] = c.b;
        }
    }
    return rgb;
}

} // namespace

TEST_CASE("jpeg: four-quadrant pattern decodes with dims and per-quadrant means within 8",
          "[jpeg]") {
    const auto rgb = quadrant_rgb();
    std::vector<uint8_t> out;
    REQUIRE(encode_jpeg_rgb888(rgb.data(), kW, kH, kRemoteScreenJpegQuality, out));

    int w = 0, h = 0, comp = 0;
    unsigned char* dec =
        stbi_load_from_memory(out.data(), static_cast<int>(out.size()), &w, &h, &comp, 3);
    REQUIRE(dec != nullptr);
    CHECK(w == kW);
    CHECK(h == kH);

    // The centre 8x8 of each quadrant, away from the chroma-subsampled edges.
    const int centres[4][2] = {{16, 12}, {48, 12}, {16, 36}, {48, 36}};
    for (const auto& c : centres) {
        long sum[3] = {0, 0, 0};
        for (int y = c[1] - 4; y < c[1] + 4; ++y) {
            for (int x = c[0] - 4; x < c[0] + 4; ++x) {
                const unsigned char* p = dec + (static_cast<size_t>(y) * kW + x) * 3;
                sum[0] += p[0];
                sum[1] += p[1];
                sum[2] += p[2];
            }
        }
        const Rgb want = quadrant_color(c[0], c[1]);
        INFO("quadrant centre " << c[0] << "," << c[1]);
        CHECK(std::abs(static_cast<int>(sum[0] / 64) - want.r) <= 8);
        CHECK(std::abs(static_cast<int>(sum[1] / 64) - want.g) <= 8);
        CHECK(std::abs(static_cast<int>(sum[2] / 64) - want.b) <= 8);
    }
    stbi_image_free(dec);
}

TEST_CASE("jpeg: output starts with FFD8 and ends with FFD9", "[jpeg]") {
    const auto rgb = quadrant_rgb();
    std::vector<uint8_t> out;
    REQUIRE(encode_jpeg_rgb888(rgb.data(), kW, kH, kRemoteScreenJpegQuality, out));
    REQUIRE(out.size() > 4);
    CHECK(out[0] == 0xFF);
    CHECK(out[1] == 0xD8);
    CHECK(out[out.size() - 2] == 0xFF);
    CHECK(out[out.size() - 1] == 0xD9);
}

TEST_CASE("jpeg: zero size and null input return false and leave out empty", "[jpeg]") {
    const auto rgb = quadrant_rgb();
    std::vector<uint8_t> out = {1, 2, 3};
    CHECK_FALSE(encode_jpeg_rgb888(rgb.data(), 0, kH, 70, out));
    CHECK(out.empty());
    out = {1};
    CHECK_FALSE(encode_jpeg_rgb888(rgb.data(), kW, -1, 70, out));
    CHECK(out.empty());
    out = {1};
    CHECK_FALSE(encode_jpeg_rgb888(nullptr, kW, kH, 70, out));
    CHECK(out.empty());
}

TEST_CASE("jpeg: RGB565 and BGRA sources convert to the same RGB888", "[jpeg]") {
    // Colours exactly representable in RGB565, so both paths agree byte for byte.
    const uint8_t colors[3][3] = {{0xFF, 0x00, 0x00}, {0x00, 0xFF, 0x00}, {0x08, 0x10, 0xFF}};
    std::vector<uint8_t> bgra, rgb565;
    for (const auto& c : colors) {
        bgra.insert(bgra.end(), {c[2], c[1], c[0], 0xFF});
        const uint16_t p =
            static_cast<uint16_t>(((c[0] >> 3) << 11) | ((c[1] >> 2) << 5) | (c[2] >> 3));
        rgb565.insert(rgb565.end(), {static_cast<uint8_t>(p & 0xFF), static_cast<uint8_t>(p >> 8)});
    }
    std::vector<uint8_t> from_bgra(9), from_565(9);
    convert_to_rgb888(bgra.data(), 12, 3, 1, 4, from_bgra.data());
    convert_to_rgb888(rgb565.data(), 6, 3, 1, 2, from_565.data());
    CHECK(from_bgra == std::vector<uint8_t>{0xFF, 0, 0, 0, 0xFF, 0, 0x08, 0x10, 0xFF});
    // RGB565 expansion replicates high bits, so 0x08/0x10 come back as 0x08/0x10.
    CHECK(from_565 == from_bgra);
}
