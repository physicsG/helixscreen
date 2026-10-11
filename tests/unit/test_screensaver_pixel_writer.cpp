// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "screensaver_pixel_writer.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <utility>
#include <vector>

#include "../catch_amalgamated.hpp"

using helix::ui::FrameTarget;
using helix::ui::PixelFormat;
using helix::ui::PixelWriter;
using helix::ui::Rgb;

namespace {

constexpr uint8_t UNTOUCHED = 0xAA;

/// A heap frame whose every byte starts as UNTOUCHED, so a write outside its pixel shows.
struct TestFrame {
    std::vector<uint8_t> bytes;
    FrameTarget target;

    TestFrame(uint32_t w, uint32_t h, uint32_t stride, PixelFormat format)
        : bytes(static_cast<size_t>(stride) * h, UNTOUCHED) {
        target = {bytes.data(), stride, w, h, format};
    }
    TestFrame(const TestFrame&) = delete;
    TestFrame& operator=(const TestFrame&) = delete;
};

} // namespace

TEST_CASE("PixelWriter writes XRGB8888 as B, G, R and an opaque X byte",
          "[screensaver][pixel_writer]") {
    TestFrame frame(4, 3, 4 * 4 + 8, PixelFormat::XRGB8888);
    PixelWriter writer(frame.target);

    writer.put(3, 2, Rgb{0x12, 0x34, 0x56});

    const size_t at = 2 * static_cast<size_t>(frame.target.stride) + 3 * 4;
    CHECK(frame.bytes[at] == 0x56);
    CHECK(frame.bytes[at + 1] == 0x34);
    CHECK(frame.bytes[at + 2] == 0x12);
    CHECK(frame.bytes[at + 3] == 0xFF);
    // Rows step by the stride, so the padding after the row's last pixel is left alone.
    CHECK(frame.bytes[at + 4] == UNTOUCHED);
    CHECK(frame.bytes[at - 1] == UNTOUCHED);
    CHECK(writer.get(3, 2) == Rgb{0x12, 0x34, 0x56});
}

TEST_CASE("PixelWriter fill covers every XRGB8888 pixel and no row padding",
          "[screensaver][pixel_writer]") {
    constexpr uint32_t W = 5;
    constexpr uint32_t H = 4;
    constexpr uint32_t STRIDE = W * 4 + 12;
    TestFrame frame(W, H, STRIDE, PixelFormat::XRGB8888);

    PixelWriter(frame.target).fill(Rgb{1, 2, 3});

    size_t wrong_pixels = 0;
    size_t touched_padding = 0;
    for (uint32_t y = 0; y < H; y++) {
        const uint8_t* row = frame.bytes.data() + static_cast<size_t>(y) * STRIDE;
        for (uint32_t x = 0; x < W; x++) {
            const uint8_t* px = row + x * 4;
            wrong_pixels += (px[0] != 3 || px[1] != 2 || px[2] != 1 || px[3] != 0xFF) ? 1 : 0;
        }
        for (uint32_t i = W * 4; i < STRIDE; i++) {
            touched_padding += row[i] != UNTOUCHED ? 1 : 0;
        }
    }
    CHECK(wrong_pixels == 0);
    CHECK(touched_padding == 0);
}

TEST_CASE("PixelWriter knows which points lie inside the frame", "[screensaver][pixel_writer]") {
    TestFrame frame(4, 3, 16, PixelFormat::XRGB8888);
    const PixelWriter writer(frame.target);
    CHECK(writer.contains(0, 0));
    CHECK(writer.contains(3, 2));
    CHECK_FALSE(writer.contains(4, 0));
    CHECK_FALSE(writer.contains(0, 3));
    CHECK_FALSE(writer.contains(-1, 0));
    CHECK_FALSE(writer.contains(0, -1));
}

TEST_CASE("PixelWriter writes RGB565 as the top 5, 6 and 5 bits, little-endian",
          "[screensaver][pixel_writer]") {
    TestFrame frame(4, 2, 4 * 2 + 2, PixelFormat::RGB565);
    PixelWriter writer(frame.target);
    const size_t stride = frame.target.stride;

    writer.put(0, 0, Rgb{0x12, 0x34, 0x56});
    writer.put(1, 1, Rgb{255, 0, 0});
    writer.put(2, 1, Rgb{0, 255, 0});
    writer.put(3, 1, Rgb{0, 0, 255});

    // 0x12 -> red 2, 0x34 -> green 13, 0x56 -> blue 10: (2 << 11) | (13 << 5) | 10 = 0x11AA
    CHECK(frame.bytes[0] == 0xAA);
    CHECK(frame.bytes[1] == 0x11);
    CHECK(frame.bytes[stride + 2] == 0x00);
    CHECK(frame.bytes[stride + 3] == 0xF8);
    CHECK(frame.bytes[stride + 4] == 0xE0);
    CHECK(frame.bytes[stride + 5] == 0x07);
    CHECK(frame.bytes[stride + 6] == 0x1F);
    CHECK(frame.bytes[stride + 7] == 0x00);
    CHECK(frame.bytes[stride + 8] == UNTOUCHED); // row padding

    // Levels expand back to 8 bits by repeating their top bits.
    CHECK(writer.get(1, 1) == Rgb{255, 0, 0});
    CHECK(writer.get(2, 1) == Rgb{0, 255, 0});
    CHECK(writer.get(0, 0) == Rgb{16, 52, 82});
}

TEST_CASE("PixelWriter dithers RGB565 with a 4x4 ordered pattern and writes XRGB8888 exactly",
          "[screensaver][pixel_writer]") {
    TestFrame frame(4, 4, 8, PixelFormat::RGB565);
    PixelWriter writer(frame.target);
    for (int32_t y = 0; y < 4; y++) {
        for (int32_t x = 0; x < 4; x++) {
            writer.put_dithered(x, y, Rgb{70, 70, 70});
        }
    }

    // 70 is 8.51 red and blue levels of 31: half the cells round up, in a checkerboard.
    constexpr uint8_t RED_BLUE[4][4] = {{8, 9, 8, 9}, {9, 8, 9, 8}, {8, 9, 8, 9}, {9, 8, 9, 8}};
    // 70 is 17.29 green levels of 63: the five cells with the highest thresholds round up.
    int green_up = 0;
    for (int32_t y = 0; y < 4; y++) {
        for (int32_t x = 0; x < 4; x++) {
            CAPTURE(x, y);
            const size_t at = static_cast<size_t>(y) * 8 + static_cast<size_t>(x) * 2;
            const uint16_t v = static_cast<uint16_t>(frame.bytes[at] | frame.bytes[at + 1] << 8);
            CHECK((v >> 11) == RED_BLUE[y][x]);
            CHECK((v & 0x1F) == RED_BLUE[y][x]);
            const int green = (v >> 5) & 0x3F;
            CHECK((green == 17 || green == 18));
            green_up += green == 18 ? 1 : 0;
        }
    }
    CHECK(green_up == 5);

    // The ends of the range never dither.
    for (uint8_t t = 0; t < 16; t++) {
        CAPTURE(static_cast<int>(t));
        CHECK(helix::ui::dither_channel(0, 31, t) == 0);
        CHECK(helix::ui::dither_channel(255, 31, t) == 31);
        CHECK(helix::ui::dither_channel(255, 63, t) == 63);
    }

    TestFrame exact(1, 1, 4, PixelFormat::XRGB8888);
    PixelWriter(exact.target).put_dithered(0, 0, Rgb{70, 71, 72});
    CHECK(PixelWriter(exact.target).get(0, 0) == Rgb{70, 71, 72});
}

TEST_CASE("PixelWriter max blending brightens each channel and leaves a dimmer colour alone",
          "[screensaver][pixel_writer]") {
    SECTION("XRGB8888") {
        TestFrame frame(2, 1, 8, PixelFormat::XRGB8888);
        PixelWriter writer(frame.target);
        writer.put(0, 0, Rgb{10, 200, 30});
        writer.blend_max(0, 0, Rgb{100, 50, 40});
        CHECK(writer.get(0, 0) == Rgb{100, 200, 40});

        const std::vector<uint8_t> before = frame.bytes;
        writer.blend_max(0, 0, Rgb{5, 5, 5});
        CHECK(frame.bytes == before);
    }
    SECTION("RGB565") {
        TestFrame frame(2, 1, 4, PixelFormat::RGB565);
        PixelWriter writer(frame.target);
        writer.put(0, 0, Rgb{0, 0, 0});
        writer.blend_max(0, 0, Rgb{255, 255, 255});
        CHECK(writer.get(0, 0) == Rgb{255, 255, 255});

        writer.put(1, 0, Rgb{255, 255, 255});
        const std::vector<uint8_t> before = frame.bytes;
        writer.blend_max(1, 0, Rgb{0, 0, 0});
        CHECK(frame.bytes == before);
    }
}

TEST_CASE("PixelWriter lines visit both ends and step at most one pixel per axis",
          "[screensaver][pixel_writer]") {
    using Points = std::vector<std::pair<int32_t, int32_t>>;
    const auto trace = [](int32_t x0, int32_t y0, int32_t x1, int32_t y1) {
        Points points;
        PixelWriter::line(x0, y0, x1, y1, [&](int32_t x, int32_t y) { points.emplace_back(x, y); });
        return points;
    };

    CHECK(trace(0, 0, 4, 2) == Points{{0, 0}, {1, 1}, {2, 1}, {3, 2}, {4, 2}});
    CHECK(trace(0, 0, 1, 3) == Points{{0, 0}, {0, 1}, {1, 2}, {1, 3}});
    CHECK(trace(2, 5, 2, 2) == Points{{2, 5}, {2, 4}, {2, 3}, {2, 2}});
    CHECK(trace(7, 7, 7, 7) == Points{{7, 7}});

    const std::vector<std::pair<Points::value_type, Points::value_type>> segments = {
        {{-3, 9}, {12, -4}}, {{5, 5}, {-6, 1}}, {{0, 0}, {-2, -9}}};
    for (const auto& [a, b] : segments) {
        const Points points = trace(a.first, a.second, b.first, b.second);
        INFO("from " << a.first << "," << a.second << " to " << b.first << "," << b.second);
        REQUIRE_FALSE(points.empty());
        CHECK(points.front() == a);
        CHECK(points.back() == b);
        const size_t expected = static_cast<size_t>(
            std::max(std::abs(b.first - a.first), std::abs(b.second - a.second)) + 1);
        CHECK(points.size() == expected);
        for (size_t i = 1; i < points.size(); i++) {
            CHECK(std::abs(points[i].first - points[i - 1].first) <= 1);
            CHECK(std::abs(points[i].second - points[i - 1].second) <= 1);
        }
    }
}
