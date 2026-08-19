// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Phase 1 harness: the REAL filament_path_canvas widget, driven by its C API,
// rendering in the browser. No app, no backend — just the widget + a thin stub
// layer (wasm/stubs_phase1.cpp).

#include "ams_types.h" // PathSegment, PathTopology
#include "ui_filament_path_canvas.h"

#include "lvgl/lvgl.h"

#include <SDL.h>
#include <emscripten.h>

static void main_loop(void) { lv_timer_handler(); }

int main(int, char**) {
    lv_init();
    lv_sdl_window_create(900, 620);
    lv_sdl_mouse_create();

    lv_obj_t* scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x2e3440), 0); // Nord polar night
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    // A sized container for the canvas to fill.
    lv_obj_t* cont = lv_obj_create(scr);
    lv_obj_set_size(cont, 380, 580);
    lv_obj_center(cont);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_all(cont, 0, 0);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* canvas = ui_filament_path_canvas_create(cont);
    lv_obj_set_size(canvas, lv_pct(100), lv_pct(100));

    // Drive the existing C API: a 4-lane HUB (ACE combiner) with lane 2 loaded.
    ui_filament_path_canvas_set_topology(canvas, static_cast<int>(PathTopology::HUB));
    ui_filament_path_canvas_set_slot_count(canvas, 4);
    ui_filament_path_canvas_set_active_slot(canvas, 2);
    ui_filament_path_canvas_set_filament_color(canvas, 0xe8913a);
    ui_filament_path_canvas_set_filament_segment(canvas, static_cast<int>(PathSegment::TOOLHEAD));

    // Per-lane installed filament (color + how far it extends).
    ui_filament_path_canvas_set_slot_filament(canvas, 0, static_cast<int>(PathSegment::LANE), 0x5e81ac);
    ui_filament_path_canvas_set_slot_filament(canvas, 1, static_cast<int>(PathSegment::LANE), 0xa3be8c);
    ui_filament_path_canvas_set_slot_filament(canvas, 2, static_cast<int>(PathSegment::TOOLHEAD), 0xe8913a);
    ui_filament_path_canvas_set_slot_filament(canvas, 3, static_cast<int>(PathSegment::LANE), 0xbf616a);


    ui_filament_path_canvas_refresh(canvas);

    emscripten_set_main_loop(main_loop, 0, 1);
    return 0;
}
