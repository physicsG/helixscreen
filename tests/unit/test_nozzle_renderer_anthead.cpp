// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../lvgl_test_fixture.h"
#include "nozzle_renderer_anthead.h"

#include <cstdint>

#include "../catch_amalgamated.hpp"

// The digest is FNV-1a over the 100x163 BGRA pixel bytes the AntHead was drawn
// from, so any change to the decoded pixels (a re-encode that alters colors,
// premultiplied alpha, a lost row) fails here.
TEST_CASE_METHOD(LVGLTestFixture, "AntHead image decodes to its reference pixels",
                 "[nozzle_renderer][anthead]") {
    const lv_draw_buf_t* img = anthead_image();
    REQUIRE(img != nullptr);
    REQUIRE(img->header.w == 100);
    REQUIRE(img->header.h == 163);
    REQUIRE(img->header.cf == LV_COLOR_FORMAT_ARGB8888);

    uint32_t hash = 0x811c9dc5u;
    for (uint32_t y = 0; y < img->header.h; ++y) {
        const uint8_t* row = img->data + y * img->header.stride;
        for (uint32_t i = 0; i < img->header.w * 4u; ++i) {
            hash = (hash ^ row[i]) * 0x01000193u;
        }
    }
    CHECK(hash == 0x994591c4u);
}
