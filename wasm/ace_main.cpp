// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Interactive multiACE page mockup (LVGL/WASM). Reproduces the ACE panel — slot
// bays, the real filament_path_canvas, and an operation sidebar — and drives a
// mocked Load/Unload run so the distance-proportional layout and distance-timed
// animation can be seen end to end. Everything here is harness scaffolding; the
// filament path itself is the real production widget.

#include "ams_types.h" // PathSegment, PathTopology
#include "ui_filament_path_canvas.h"

#include "lvgl/lvgl.h"

#include <SDL.h>
#include <emscripten.h>

#include <cstdio>
#include <cstring>

// ---- palette (Nord) ----
#define C_BG 0x2e3440
#define C_PANEL 0x3b4252
#define C_PANEL2 0x434c5e
#define C_TEXT 0xeceff4
#define C_MUTED 0x8891a0
#define C_FROST 0x88c0d0
#define C_ACCENT 0x5e81ac
#define C_GREEN 0xa3be8c
#define C_AMBER 0xe8913a

namespace {


enum class Op { IDLE, LOADING, UNLOADING, LOADED };

// One phase of a mocked operation. seg < 0 = no tip motion (a prep hold);
// seg >= 0 = drive the path canvas to that PathSegment. dwell_ms is how long the
// phase runs — for a motion phase it covers the (distance-timed) animation, so
// the long bowden phase dwells ~8 s and the short seat ~0.7 s.
struct Phase {
    const char* label;
    int seg;       // target PathSegment, or -1 for a prep hold
    int dwell_ms;  // duration of this phase
    bool heat;     // nozzle heat glow during this phase
    int s_ace;     // sensor targets applied AFTER the phase completes
    int s_ext;
    int s_tool;
};

const Phase LOAD_SEQ[] = {
    {"Home", -1, 500, false, 1, 0, 0},
    {"Select", -1, 500, false, 1, 0, 0},
    {"Heat nozzle", -1, 1500, true, 1, 0, 0},
    {"Feed to extruder", (int)PathSegment::HUB, 800, true, 1, 1, 0},
    {"Feed through bowden", (int)PathSegment::TOOLHEAD, 1500, true, 1, 1, 1},
    {"Purge", (int)PathSegment::NOZZLE, 800, true, 1, 1, 1},
};
const Phase UNLOAD_SEQ[] = {
    {"Heat nozzle", -1, 1200, true, 1, 1, 1},
    {"Form tip", -1, 1000, true, 1, 1, 1},
    {"Retract through bowden", (int)PathSegment::HUB, 1500, false, 1, 1, 0},
    {"Retract to ACE", (int)PathSegment::SPOOL, 800, false, 1, 0, 0},
};

struct AcePage {
    lv_obj_t* canvas = nullptr;
    lv_obj_t* status = nullptr;
    lv_obj_t* op_title = nullptr;
    lv_obj_t* step_box = nullptr;
    lv_obj_t* btn_load = nullptr;
    lv_obj_t* btn_unload = nullptr;
    lv_obj_t* chip_ace = nullptr;
    lv_obj_t* chip_ext = nullptr;
    lv_obj_t* chip_tool = nullptr;
    lv_timer_t* timer = nullptr;

    Op op = Op::IDLE;
    const Phase* seq = nullptr;
    int seq_len = 0;
    int idx = 0;
    bool phase_started = false;
    int hold_elapsed = 0;
    int active_slot = 2; // T3 = the amber bay
};

AcePage g;

void set_chip(lv_obj_t* chip, bool on) {
    lv_obj_set_style_bg_color(chip, lv_color_hex(on ? C_GREEN : C_PANEL2), 0);
    lv_obj_set_style_bg_opa(chip, on ? LV_OPA_30 : LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(chip, lv_color_hex(on ? C_GREEN : 0x50596b), 0);
    lv_obj_t* lbl = lv_obj_get_child(chip, 0);
    if (lbl)
        lv_obj_set_style_text_color(lbl, lv_color_hex(on ? C_GREEN : C_MUTED), 0);
}

void rebuild_stepper(const Phase* seq, int len, bool unload) {
    lv_obj_clean(g.step_box);
    (void)unload;
    for (int i = 0; i < len; ++i) {
        lv_obj_t* row = lv_obj_create(g.step_box);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
        lv_obj_set_style_pad_ver(row, 4, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(row, 8, 0);

        lv_obj_t* dot = lv_obj_create(row);
        lv_obj_remove_style_all(dot);
        lv_obj_set_size(dot, 8, 8);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(dot, lv_color_hex(0x50596b), 0);

        lv_obj_t* lbl = lv_label_create(row);
        lv_label_set_text(lbl, seq[i].label);
        lv_obj_set_style_text_color(lbl, lv_color_hex(C_MUTED), 0);
    }
}

void highlight_step(int active) {
    uint32_t n = lv_obj_get_child_count(g.step_box);
    for (uint32_t i = 0; i < n; ++i) {
        lv_obj_t* row = lv_obj_get_child(g.step_box, i);
        lv_obj_t* dot = lv_obj_get_child(row, 0);
        lv_obj_t* lbl = lv_obj_get_child(row, 1);
        bool done = (int)i < active;
        bool cur = (int)i == active;
        lv_color_t c = lv_color_hex(cur ? C_FROST : (done ? C_GREEN : 0x50596b));
        lv_obj_set_style_bg_color(dot, c, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(cur ? C_TEXT : (done ? C_GREEN : C_MUTED)), 0);
    }
}

void finish_run(bool loaded) {
    g.op = loaded ? Op::LOADED : Op::IDLE;
    g.seq = nullptr;
    lv_label_set_text(g.status, loaded ? "Loaded  .  T3 ready" : "Ready");
    lv_label_set_text(g.op_title, loaded ? "LOADED" : "IDLE");
    ui_filament_path_canvas_set_heat_active(g.canvas, false);
    highlight_step(loaded ? 999 : -1);
    lv_obj_set_state(g.btn_load, LV_STATE_DISABLED, loaded);
    lv_obj_set_state(g.btn_unload, LV_STATE_DISABLED, !loaded);
}

void apply_sensors(const Phase& p) {
    set_chip(g.chip_ace, p.s_ace);
    set_chip(g.chip_ext, p.s_ext);
    set_chip(g.chip_tool, p.s_tool);
}

void start_op(Op op) {
    g.op = op;
    g.idx = 0;
    g.phase_started = false;
    g.hold_elapsed = 0;
    if (op == Op::LOADING) {
        g.seq = LOAD_SEQ;
        g.seq_len = (int)(sizeof(LOAD_SEQ) / sizeof(LOAD_SEQ[0]));
        lv_label_set_text(g.op_title, "LOADING  T3");
        rebuild_stepper(LOAD_SEQ, g.seq_len, false);
        ui_filament_path_canvas_set_active_slot(g.canvas, g.active_slot);
        ui_filament_path_canvas_set_filament_color(g.canvas, C_AMBER);
        ui_filament_path_canvas_set_filament_segment(g.canvas, (int)PathSegment::SPOOL);
    } else {
        g.seq = UNLOAD_SEQ;
        g.seq_len = (int)(sizeof(UNLOAD_SEQ) / sizeof(UNLOAD_SEQ[0]));
        lv_label_set_text(g.op_title, "UNLOADING  T3");
        rebuild_stepper(UNLOAD_SEQ, g.seq_len, true);
    }
    lv_obj_set_state(g.btn_load, LV_STATE_DISABLED, true);
    lv_obj_set_state(g.btn_unload, LV_STATE_DISABLED, true);
    highlight_step(0);
}

void tick(lv_timer_t*) {
    if (g.op != Op::LOADING && g.op != Op::UNLOADING)
        return;
    if (g.idx >= g.seq_len) {
        finish_run(g.op == Op::LOADING);
        return;
    }
    const Phase& p = g.seq[g.idx];
    lv_label_set_text(g.status, p.label);
    ui_filament_path_canvas_set_heat_active(g.canvas, p.heat);
    highlight_step(g.idx);

    // On entry, kick off the tip motion (if any); then dwell for the phase.
    // dwell covers the distance-timed animation — we don't poll is_animating(),
    // which also reports the infinite flow particles and would never settle.
    if (!g.phase_started) {
        if (p.seg >= 0)
            ui_filament_path_canvas_set_filament_segment(g.canvas, p.seg);
        g.phase_started = true;
        g.hold_elapsed = 0;
        return;
    }
    g.hold_elapsed += 80;
    if (g.hold_elapsed >= p.dwell_ms) {
        apply_sensors(p);
        g.hold_elapsed = 0;
        g.idx++;
        g.phase_started = false;
    }
}

// ---- button callbacks ----
void on_load(lv_event_t*) {
    if (g.op == Op::LOADING || g.op == Op::UNLOADING)
        return;
    start_op(Op::LOADING);
}
void on_unload(lv_event_t*) {
    if (g.op == Op::LOADING || g.op == Op::UNLOADING)
        return;
    start_op(Op::UNLOADING);
}
void on_reset(lv_event_t*) {
    g.op = Op::IDLE;
    g.seq = nullptr;
    ui_filament_path_canvas_set_filament_segment(g.canvas, (int)PathSegment::SPOOL);
    ui_filament_path_canvas_set_heat_active(g.canvas, false);
    set_chip(g.chip_ace, true);
    set_chip(g.chip_ext, false);
    set_chip(g.chip_tool, false);
    lv_label_set_text(g.status, "Ready");
    lv_label_set_text(g.op_title, "IDLE");
    lv_obj_clean(g.step_box);
    lv_obj_set_state(g.btn_load, LV_STATE_DISABLED, false);
    lv_obj_set_state(g.btn_unload, LV_STATE_DISABLED, false);
}

// ---- UI construction helpers ----
lv_obj_t* make_button(lv_obj_t* parent, const char* text, uint32_t accent, lv_event_cb_t cb) {
    lv_obj_t* b = lv_button_create(parent);
    lv_obj_set_width(b, lv_pct(100));
    lv_obj_set_height(b, 44);
    lv_obj_set_style_bg_color(b, lv_color_hex(C_PANEL2), 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(accent), LV_STATE_DISABLED);
    lv_obj_set_style_bg_opa(b, LV_OPA_40, LV_STATE_DISABLED);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_border_color(b, lv_color_hex(accent), 0);
    lv_obj_set_style_radius(b, 8, 0);
    if (cb)
        lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* l = lv_label_create(b);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_color(l, lv_color_hex(C_TEXT), 0);
    lv_obj_center(l);
    return b;
}

lv_obj_t* make_sensor_chip(lv_obj_t* parent, const char* text) {
    lv_obj_t* c = lv_obj_create(parent);
    lv_obj_remove_style_all(c);
    lv_obj_set_flex_grow(c, 1);
    lv_obj_set_height(c, 34);
    lv_obj_set_style_radius(c, 7, 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(c, lv_color_hex(C_PANEL2), 0);
    lv_obj_set_style_border_width(c, 1, 0);
    lv_obj_set_style_border_color(c, lv_color_hex(0x50596b), 0);
    lv_obj_t* l = lv_label_create(c);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_color(l, lv_color_hex(C_MUTED), 0);
    lv_obj_center(l);
    return c;
}

void make_slot_chip(lv_obj_t* parent, uint32_t color, const char* mat, int num) {
    lv_obj_t* s = lv_obj_create(parent);
    lv_obj_remove_style_all(s);
    lv_obj_set_size(s, 74, 78);
    lv_obj_set_style_radius(s, 10, 0);
    lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s, lv_color_hex(C_PANEL), 0);
    lv_obj_set_style_border_width(s, 2, 0);
    lv_obj_set_style_border_color(s, lv_color_hex(color), 0);
    lv_obj_set_flex_flow(s, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(s, 6, 0);
    lv_obj_set_style_pad_row(s, 4, 0);

    lv_obj_t* disc = lv_obj_create(s);
    lv_obj_remove_style_all(disc);
    lv_obj_set_size(disc, 34, 34);
    lv_obj_set_style_radius(disc, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(disc, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(disc, lv_color_hex(color), 0);
    lv_obj_set_style_border_width(disc, 3, 0);
    lv_obj_set_style_border_color(disc, lv_color_hex(0x2b303b), 0);

    lv_obj_t* ml = lv_label_create(s);
    lv_label_set_text(ml, mat);
    lv_obj_set_style_text_color(ml, lv_color_hex(C_TEXT), 0);
    char buf[8];
    std::snprintf(buf, sizeof(buf), "T%d", num);
    lv_obj_t* nl = lv_label_create(s);
    lv_label_set_text(nl, buf);
    lv_obj_set_style_text_color(nl, lv_color_hex(C_MUTED), 0);
}

void build_ui() {
    lv_obj_t* scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(C_BG), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(scr, 14, 0);
    lv_obj_set_style_pad_row(scr, 10, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    // Header
    lv_obj_t* title = lv_label_create(scr);
    lv_label_set_text(title, "multiACE  .  ACE 0  .  Snapmaker U1");
    lv_obj_set_style_text_color(title, lv_color_hex(C_FROST), 0);

    // Body: left (slots + path) | right (sidebar)
    lv_obj_t* body = lv_obj_create(scr);
    lv_obj_remove_style_all(body);
    lv_obj_set_width(body, lv_pct(100));
    lv_obj_set_flex_grow(body, 1);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(body, 12, 0);
    lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE);

    // Left column
    lv_obj_t* left = lv_obj_create(body);
    lv_obj_remove_style_all(left);
    lv_obj_set_flex_grow(left, 1);
    lv_obj_set_height(left, lv_pct(100));
    lv_obj_set_flex_flow(left, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(left, 8, 0);
    lv_obj_clear_flag(left, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* slots = lv_obj_create(left);
    lv_obj_remove_style_all(slots);
    lv_obj_set_width(slots, lv_pct(100));
    lv_obj_set_height(slots, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(slots, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(slots, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    make_slot_chip(slots, 0x5e81ac, "PLA", 0);
    make_slot_chip(slots, 0xa3be8c, "PETG", 1);
    make_slot_chip(slots, 0xe8913a, "PLA", 2);
    make_slot_chip(slots, 0xbf616a, "ABS", 3);

    lv_obj_t* pathwrap = lv_obj_create(left);
    lv_obj_remove_style_all(pathwrap);
    lv_obj_set_width(pathwrap, lv_pct(100));
    lv_obj_set_flex_grow(pathwrap, 1);
    lv_obj_clear_flag(pathwrap, LV_OBJ_FLAG_SCROLLABLE);

    g.canvas = ui_filament_path_canvas_create(pathwrap);
    lv_obj_set_size(g.canvas, lv_pct(100), lv_pct(100));
    ui_filament_path_canvas_set_topology(g.canvas, (int)PathTopology::HUB);
    ui_filament_path_canvas_set_slot_count(g.canvas, 4);
    ui_filament_path_canvas_set_active_slot(g.canvas, g.active_slot);
    ui_filament_path_canvas_set_filament_color(g.canvas, C_AMBER);
    ui_filament_path_canvas_set_filament_segment(g.canvas, (int)PathSegment::SPOOL);
    ui_filament_path_canvas_set_slot_filament(g.canvas, 0, (int)PathSegment::LANE, 0x5e81ac);
    ui_filament_path_canvas_set_slot_filament(g.canvas, 1, (int)PathSegment::LANE, 0xa3be8c);
    ui_filament_path_canvas_set_slot_filament(g.canvas, 2, (int)PathSegment::LANE, 0xe8913a);
    ui_filament_path_canvas_set_slot_filament(g.canvas, 3, (int)PathSegment::LANE, 0xbf616a);

    // Right sidebar
    lv_obj_t* side = lv_obj_create(body);
    lv_obj_remove_style_all(side);
    lv_obj_set_size(side, 320, lv_pct(100));
    lv_obj_set_style_bg_color(side, lv_color_hex(C_PANEL), 0);
    lv_obj_set_style_bg_opa(side, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(side, 12, 0);
    lv_obj_set_style_pad_all(side, 14, 0);
    lv_obj_set_flex_flow(side, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(side, 10, 0);
    lv_obj_clear_flag(side, LV_OBJ_FLAG_SCROLLABLE);

    g.op_title = lv_label_create(side);
    lv_label_set_text(g.op_title, "IDLE");
    lv_obj_set_style_text_color(g.op_title, lv_color_hex(C_FROST), 0);

    g.status = lv_label_create(side);
    lv_label_set_text(g.status, "Ready");
    lv_obj_set_style_text_color(g.status, lv_color_hex(C_TEXT), 0);
    lv_label_set_long_mode(g.status, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(g.status, lv_pct(100));

    // Sensor chips
    lv_obj_t* chips = lv_obj_create(side);
    lv_obj_remove_style_all(chips);
    lv_obj_set_width(chips, lv_pct(100));
    lv_obj_set_height(chips, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(chips, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(chips, 6, 0);
    g.chip_ace = make_sensor_chip(chips, "In ACE");
    g.chip_ext = make_sensor_chip(chips, "Extruder");
    g.chip_tool = make_sensor_chip(chips, "Toolhead");
    set_chip(g.chip_ace, true);

    // Step stepper
    g.step_box = lv_obj_create(side);
    lv_obj_remove_style_all(g.step_box);
    lv_obj_set_width(g.step_box, lv_pct(100));
    lv_obj_set_flex_grow(g.step_box, 1);
    lv_obj_set_flex_flow(g.step_box, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(g.step_box, LV_OBJ_FLAG_SCROLLABLE);

    // Action buttons
    g.btn_load = make_button(side, "Load  T3", C_GREEN, on_load);
    g.btn_unload = make_button(side, "Unload  T3", C_AMBER, on_unload);
    lv_obj_set_state(g.btn_unload, LV_STATE_DISABLED, true);
    make_button(side, "Reset", C_ACCENT, on_reset);

    // Mocked (inert) controls, present for completeness.
    lv_obj_t* extras = lv_obj_create(side);
    lv_obj_remove_style_all(extras);
    lv_obj_set_width(extras, lv_pct(100));
    lv_obj_set_height(extras, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(extras, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(extras, 6, 0);
    make_button(extras, "Dry", C_ACCENT, nullptr);
    make_button(extras, "Check", C_ACCENT, nullptr);
    make_button(extras, "Bypass", C_ACCENT, nullptr);

    g.timer = lv_timer_create(tick, 80, nullptr);
}

void main_loop(void) { lv_timer_handler(); }

} // namespace

// JS bridge: the on-screen buttons already work (LVGL handles canvas clicks);
// these let a headless harness (or a custom shell's controls) drive a run too.
extern "C" {
EMSCRIPTEN_KEEPALIVE void ace_load(void) {
    if (g.op != Op::LOADING && g.op != Op::UNLOADING)
        start_op(Op::LOADING);
}
EMSCRIPTEN_KEEPALIVE void ace_unload(void) {
    if (g.op != Op::LOADING && g.op != Op::UNLOADING)
        start_op(Op::UNLOADING);
}
EMSCRIPTEN_KEEPALIVE void ace_reset(void) { on_reset(nullptr); }
}

int main(int, char**) {
    lv_init();
    lv_sdl_window_create(940, 640);
    lv_sdl_mouse_create();
    build_ui();
    emscripten_set_main_loop(main_loop, 0, 1);
    return 0;
}
