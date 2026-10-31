// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#if HELIX_ENABLE_SCREENSAVER

#include "screensaver.h"

#include "../catch_amalgamated.hpp"

// lv_canvas_set_buffer() derives its own row stride (rounded to LV_DRAW_BUF_STRIDE_ALIGN) and
// sizes the canvas extent from stride * h, so the allocation a saver hands it must use the same
// stride or lv_canvas_fill_bg() overruns the buffer (prestonbrown/helixscreen#1591). That every
// canvas saver's allocation covers the extent is in test_screensaver_contract.cpp.

TEST_CASE("screensaver_canvas_stride_bytes matches lv_canvas_set_buffer's stride",
          "[screensaver][1591]") {
    // The widths where a tight w * bytes-per-pixel pitch diverges from LVGL's under a
    // padded-stride config, for both canvas formats.
    for (lv_color_format_t cf : {LV_COLOR_FORMAT_XRGB8888, LV_COLOR_FORMAT_RGB565}) {
        for (int w : {1, 3, 100, 480, 630, 800, 1024}) {
            CAPTURE(static_cast<int>(cf), w);
            REQUIRE(helix::ui::screensaver_canvas_stride_bytes(w, cf) ==
                    lv_draw_buf_width_to_stride(w, cf));
        }
    }
}

#endif // HELIX_ENABLE_SCREENSAVER
