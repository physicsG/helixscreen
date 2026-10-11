// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../test_fixtures.h"
#include "nozzle_renderer_a4t.h"
#include "nozzle_renderer_bambu.h"
#include "nozzle_renderer_creality_k1.h"
#include "nozzle_renderer_creality_k2.h"
#include "nozzle_renderer_jabberwocky.h"
#include "nozzle_renderer_stealthburner.h"

#include <cstdint>
#include <cstring>
#include <optional>
#include <vector>

#include "../catch_amalgamated.hpp"

namespace {

using DrawFn = void (*)(lv_layer_t*, int32_t, int32_t, std::optional<lv_color_t>, int32_t,
                        lv_opa_t);

constexpr int32_t SIZE = 200;

std::vector<uint8_t> render(lv_obj_t* canvas, DrawFn draw, std::optional<lv_color_t> filament) {
    lv_canvas_fill_bg(canvas, lv_color_hex(0x1A1A1A), LV_OPA_COVER);
    lv_layer_t layer;
    lv_canvas_init_layer(canvas, &layer);
    draw(&layer, SIZE / 2, SIZE / 2, filament, 16, LV_OPA_COVER);
    lv_canvas_finish_layer(canvas, &layer);
    const lv_draw_buf_t* buf = lv_canvas_get_draw_buf(canvas);
    return std::vector<uint8_t>(buf->data, buf->data + buf->header.stride * SIZE);
}

} // namespace

// Black and mid-grey are real filament colours: a loaded tip must not fall
// back to the unloaded metal just because of the colour it carries.
TEST_CASE_METHOD(XMLTestFixture, "Nozzle tip shows loaded black and grey filament",
                 "[nozzle_renderer]") {
    lv_obj_t* canvas = lv_canvas_create(test_screen());
    lv_draw_buf_t* draw_buf =
        lv_draw_buf_create(SIZE, SIZE, LV_COLOR_FORMAT_ARGB8888, LV_STRIDE_AUTO);
    REQUIRE(draw_buf != nullptr);
    lv_canvas_set_draw_buf(canvas, draw_buf);

    const struct {
        const char* name;
        DrawFn draw;
    } renderers[] = {
        {"a4t", draw_nozzle_a4t},
        {"bambu", draw_nozzle_bambu},
        {"creality_k1", draw_nozzle_creality_k1},
        {"creality_k2", draw_nozzle_creality_k2},
        {"jabberwocky", draw_nozzle_jabberwocky},
        {"stealthburner", draw_nozzle_stealthburner},
    };

    for (const auto& r : renderers) {
        const auto unloaded = render(canvas, r.draw, std::nullopt);
        for (uint32_t hex : {0x000000u, 0x808080u, 0x3A3A3Au}) {
            INFO(r.name << " filament 0x" << std::hex << hex);
            const bool differs = render(canvas, r.draw, lv_color_hex(hex)) != unloaded;
            CHECK(differs);
        }
    }

    lv_obj_delete(canvas);
    lv_draw_buf_destroy(draw_buf);
}
