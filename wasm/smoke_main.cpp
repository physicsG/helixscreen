// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Phase 0 smoke harness: prove emcc + LVGL 9.5 + SDL2 + emscripten_set_main_loop
// render in a browser. No HelixScreen code yet — just LVGL built-ins.

#include "lvgl/lvgl.h"

#include <SDL.h>
#include <emscripten.h>

static void main_loop(void) {
    lv_timer_handler(); // LVGL's SDL driver pumps events + renders to the canvas
}

int main(int, char**) {
    lv_init();
    lv_display_t* disp = lv_sdl_window_create(800, 480);
    (void)disp;
    lv_sdl_mouse_create();

    lv_obj_t* scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0e1116), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    lv_obj_t* title = lv_label_create(scr);
    lv_label_set_text(title, "HelixScreen  .  LVGL running in WebAssembly");
    lv_obj_set_style_text_color(title, lv_color_hex(0x37c2d6), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 28);

    lv_obj_t* btn = lv_button_create(scr);
    lv_obj_set_size(btn, 160, 52);
    lv_obj_align(btn, LV_ALIGN_CENTER, 0, -10);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x1a3b42), 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(0x37c2d6), 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_t* btnlbl = lv_label_create(btn);
    lv_label_set_text(btnlbl, "Interactive");
    lv_obj_center(btnlbl);

    lv_obj_t* arc = lv_arc_create(scr);
    lv_obj_set_size(arc, 128, 128);
    lv_arc_set_rotation(arc, 135);
    lv_arc_set_bg_angles(arc, 0, 270);
    lv_arc_set_value(arc, 68);
    lv_obj_set_style_arc_color(arc, lv_color_hex(0xe8913a), LV_PART_INDICATOR);
    lv_obj_align(arc, LV_ALIGN_BOTTOM_MID, 0, -18);

    emscripten_set_main_loop(main_loop, 0, 1);
    return 0;
}
