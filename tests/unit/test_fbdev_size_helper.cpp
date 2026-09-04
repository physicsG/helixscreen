// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_fbdev_size_helper.cpp
 * @brief Unit tests for the fbdev kernel-size helper used to detect a
 *        mismatch between the user's requested resolution and the actual
 *        kernel framebuffer.
 */

#ifdef __linux__

#include "../../include/fbdev_size_helper.h"

#include <linux/fb.h>

#include "../catch_amalgamated.hpp"

using namespace helix;

static struct fb_var_screeninfo make_vinfo(uint32_t xres, uint32_t yres, uint32_t xvirt,
                                           uint32_t yvirt) {
    struct fb_var_screeninfo v {};
    v.xres = xres;
    v.yres = yres;
    v.xres_virtual = xvirt;
    v.yres_virtual = yvirt;
    return v;
}

TEST_CASE("fb_size_from_var_screeninfo: uses xres/yres (physical)", "[fbdev_size]") {
    auto v = make_vinfo(800, 480, 800, 480);
    auto size = fb_size_from_var_screeninfo(v);
    REQUIRE(size.width == 800);
    REQUIRE(size.height == 480);
}

TEST_CASE("fb_size_from_var_screeninfo: virtual larger than physical still returns physical",
          "[fbdev_size]") {
    // Some drivers report xres_virtual > xres (double-buffered fb).
    // We want the physical/displayed size, not the virtual size.
    auto v = make_vinfo(1024, 600, 1024, 1200);
    auto size = fb_size_from_var_screeninfo(v);
    REQUIRE(size.width == 1024);
    REQUIRE(size.height == 600);
}

TEST_CASE("fb_size_from_var_screeninfo: zero dimensions flagged invalid", "[fbdev_size]") {
    auto v = make_vinfo(0, 0, 0, 0);
    auto size = fb_size_from_var_screeninfo(v);
    REQUIRE_FALSE(size.valid);
}

TEST_CASE("fb_size_from_var_screeninfo: nonzero dimensions are valid", "[fbdev_size]") {
    auto v = make_vinfo(800, 480, 800, 480);
    auto size = fb_size_from_var_screeninfo(v);
    REQUIRE(size.valid);
}

#endif // __linux__
