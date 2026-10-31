// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_remote_screen_blit.cpp
 * @brief blit_remote_frame: format conversion, source origin and both bounds guards.
 *
 * Every remote-screen sink copies dirty rects through this one function, into an
 * mmapped framebuffer or a heap shadow. A heap shadow has no mapping slack, so the
 * target bound matters as much as the source one.
 */

#include "remote_screen_blit.h"
#include "remote_screen_sink.h"

#include <cstdint>
#include <vector>

#include "../catch_amalgamated.hpp"

using namespace helix;

namespace {

RemoteScreenFrame frame_of(const uint8_t* px, int32_t x1, int32_t y1, int32_t x2, int32_t y2,
                           uint32_t src_stride, RemoteScreenPixelFormat fmt, int32_t disp_w,
                           int32_t disp_h) {
    RemoteScreenFrame f;
    f.px_map = px;
    f.x1 = x1;
    f.y1 = y1;
    f.x2 = x2;
    f.y2 = y2;
    f.disp_w = disp_w;
    f.disp_h = disp_h;
    f.src_stride = src_stride;
    f.src_format = fmt;
    return f;
}

struct Target {
    std::vector<uint8_t> bytes;
    BlitTarget view;
    Target(int w, int h, int bpp) : bytes(static_cast<size_t>(w) * h * (bpp / 8), 0) {
        view =
            BlitTarget{bytes.data(), bytes.size(), static_cast<uint32_t>(w * (bpp / 8)), w, h, bpp};
    }
};

} // namespace

TEST_CASE("blit: every src x dst format pair converts one known pixel", "[remote_screen][blit]") {
    SECTION("BGRA to 32bpp copies the bytes") {
        const std::vector<uint8_t> src = {0x11, 0x22, 0x33, 0x44};
        Target t(1, 1, 32);
        auto r = blit_remote_frame(
            frame_of(src.data(), 0, 0, 0, 0, 4, RemoteScreenPixelFormat::BGRA8888, 1, 1), t.view);
        REQUIRE(r.status == BlitStatus::Copied);
        CHECK(t.bytes == src);
    }
    SECTION("RGB565 pure red to 32bpp expands to opaque BGRA red") {
        const std::vector<uint8_t> src = {0x00, 0xF8}; // 0xF800 little-endian
        Target t(1, 1, 32);
        auto r = blit_remote_frame(
            frame_of(src.data(), 0, 0, 0, 0, 2, RemoteScreenPixelFormat::RGB565, 1, 1), t.view);
        REQUIRE(r.status == BlitStatus::Copied);
        CHECK(t.bytes == std::vector<uint8_t>{0x00, 0x00, 0xFF, 0xFF});
    }
    SECTION("RGB565 to 16bpp copies verbatim") {
        const std::vector<uint8_t> src = {0x34, 0x12};
        Target t(1, 1, 16);
        auto r = blit_remote_frame(
            frame_of(src.data(), 0, 0, 0, 0, 2, RemoteScreenPixelFormat::RGB565, 1, 1), t.view);
        REQUIRE(r.status == BlitStatus::Copied);
        CHECK(t.bytes == src);
    }
    SECTION("BGRA to 16bpp packs RGB565 little-endian") {
        const std::vector<uint8_t> src = {0x10, 0x20, 0xF0, 0xFF}; // B, G, R, A
        Target t(1, 1, 16);
        auto r = blit_remote_frame(
            frame_of(src.data(), 0, 0, 0, 0, 4, RemoteScreenPixelFormat::BGRA8888, 1, 1), t.view);
        REQUIRE(r.status == BlitStatus::Copied);
        const uint16_t p =
            static_cast<uint16_t>(((0xF0 >> 3) << 11) | ((0x20 >> 2) << 5) | (0x10 >> 3));
        CHECK(t.bytes ==
              std::vector<uint8_t>{static_cast<uint8_t>(p & 0xFF), static_cast<uint8_t>(p >> 8)});
    }
}

TEST_CASE("blit: partial-mode origin reads the rect's own row 0", "[remote_screen][blit]") {
    // A 4x2 source holding only the dirty rect, which sits at (10,20) on a 32x32 display.
    std::vector<uint8_t> src(4 * 2 * 4);
    for (size_t i = 0; i < src.size(); ++i) {
        src[i] = static_cast<uint8_t>(i + 1);
    }
    auto f = frame_of(src.data(), 10, 20, 13, 21, 16, RemoteScreenPixelFormat::BGRA8888, 32, 32);
    f.px_map_x = 10;
    f.px_map_y = 20;
    f.px_map_len = src.size();
    Target t(32, 32, 32);
    auto r = blit_remote_frame(f, t.view);
    REQUIRE(r.status == BlitStatus::Copied);
    const size_t at = static_cast<size_t>(20) * t.view.stride + 10 * 4;
    CHECK(t.bytes[at] == 1);
    CHECK(t.bytes[at + t.view.stride] == src[16]); // second rect row comes from source row 1
}

TEST_CASE("blit: px_map_len smaller than the rect is SourceOutOfBounds and writes nothing",
          "[remote_screen][blit]") {
    std::vector<uint8_t> src(8 * 8 * 4, 0xAB);
    auto f = frame_of(src.data(), 0, 0, 7, 7, 32, RemoteScreenPixelFormat::BGRA8888, 8, 8);
    f.px_map_len = src.size() - 1;
    Target t(8, 8, 32);
    auto r = blit_remote_frame(f, t.view);
    CHECK(r.status == BlitStatus::SourceOutOfBounds);
    CHECK(t.bytes == std::vector<uint8_t>(t.bytes.size(), 0));
}

TEST_CASE("blit: target len smaller than stride*h is TargetOutOfBounds and writes nothing",
          "[remote_screen][blit]") {
    std::vector<uint8_t> src(8 * 8 * 4, 0xAB);
    auto f = frame_of(src.data(), 0, 6, 7, 7, 32, RemoteScreenPixelFormat::BGRA8888, 8, 8);
    Target t(8, 8, 32);
    t.view.len = static_cast<size_t>(t.view.stride) * 8 - 1;
    auto r = blit_remote_frame(f, t.view);
    CHECK(r.status == BlitStatus::TargetOutOfBounds);
    CHECK(t.bytes == std::vector<uint8_t>(t.bytes.size(), 0));
}

TEST_CASE("blit: rect clamped at the right/bottom edge reports the clamped w/h",
          "[remote_screen][blit]") {
    std::vector<uint8_t> src(16 * 16 * 4, 0x55);
    auto f = frame_of(src.data(), 4, 6, 15, 15, 64, RemoteScreenPixelFormat::BGRA8888, 16, 16);
    Target t(8, 8, 32);
    auto r = blit_remote_frame(f, t.view);
    REQUIRE(r.status == BlitStatus::Copied);
    CHECK(r.x == 4);
    CHECK(r.y == 6);
    CHECK(r.w == 4);
    CHECK(r.h == 2);
}

TEST_CASE("blit: Unknown format and null px_map return their statuses", "[remote_screen][blit]") {
    std::vector<uint8_t> src(16, 0);
    Target t(2, 2, 32);
    CHECK(blit_remote_frame(
              frame_of(src.data(), 0, 0, 1, 1, 8, RemoteScreenPixelFormat::Unknown, 2, 2), t.view)
              .status == BlitStatus::UnknownFormat);
    CHECK(blit_remote_frame(
              frame_of(nullptr, 0, 0, 1, 1, 8, RemoteScreenPixelFormat::BGRA8888, 2, 2), t.view)
              .status == BlitStatus::NoSource);
    CHECK(remote_frame_src_bpp(RemoteScreenPixelFormat::BGRA8888) == 4);
    CHECK(remote_frame_src_bpp(RemoteScreenPixelFormat::RGB565) == 2);
    CHECK(remote_frame_src_bpp(RemoteScreenPixelFormat::Unknown) == 0);
}
