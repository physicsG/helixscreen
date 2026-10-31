// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

// A picture of every swatch finish, for eyes rather than assertions: a circle,
// a labelled-chip rounded rect and a 3D spool per filament, on the theme's own
// screen background, in light and dark. Hidden; run on demand:
//
//   HELIX_SWATCH_GALLERY_DIR=/tmp/out ./build/bin/helix-tests '[swatch_gallery]'

#include "ui_spool_canvas.h"
#include "ui_swatch.h"

#include "../lvgl_ui_test_fixture.h"
#include "../test_helpers/scoped_theme_mode.h"
#include "lodepng_encode.h"
#include "theme_manager.h"

#include <cstdlib>
#include <fstream>
#include <string>

#include "../catch_amalgamated.hpp"

namespace {

struct GalleryFilament {
    const char* name;
    uint32_t rgb;
    const char* multi;
};

constexpr GalleryFilament FILAMENTS[] = {
    {"PLA", 0xE53935, ""},
    {"Silk PLA", 0xD4AF37, ""},
    {"PLA Matte", 0x2E7D32, ""},
    {"PLA Marble", 0xE8E4DC, ""},
    {"Galaxy Black PETG", 0x1C1C2A, ""},
    {"Silk Tri-Colour", 0xD4AF37, "#D4AF37,#C0C0C0,#B87333"},
    {"PLA Dual", 0xE53935, "#E53935,#1E88E5"},
    {"PLA White", 0xFFFFFF, ""},
};

// Composited over @p bg: the snapshot keeps the page's transparency.
void write_png(const std::string& path, const lv_draw_buf_t* snap, lv_color_t bg) {
    const int w = static_cast<int>(snap->header.w);
    const int h = static_cast<int>(snap->header.h);
    std::vector<unsigned char> rgba(static_cast<size_t>(w) * h * 4);
    for (int y = 0; y < h; ++y) {
        const auto* row =
            reinterpret_cast<const lv_color32_t*>(snap->data + y * snap->header.stride);
        for (int x = 0; x < w; ++x) {
            unsigned char* d = &rgba[(static_cast<size_t>(y) * w + x) * 4];
            const int a = row[x].alpha;
            d[0] = static_cast<unsigned char>((row[x].red * a + bg.red * (255 - a)) / 255);
            d[1] = static_cast<unsigned char>((row[x].green * a + bg.green * (255 - a)) / 255);
            d[2] = static_cast<unsigned char>((row[x].blue * a + bg.blue * (255 - a)) / 255);
            d[3] = 255;
        }
    }
    unsigned char* out = nullptr;
    size_t out_size = 0;
    if (lodepng_encode32(&out, &out_size, rgba.data(), w, h) == 0) {
        std::ofstream(path, std::ios::binary).write(reinterpret_cast<char*>(out), out_size);
    }
    free(out);
}

} // namespace

TEST_CASE_METHOD(LVGLUITestFixture, "swatch gallery", "[.][swatch_gallery]") {
    const char* dir = std::getenv("HELIX_SWATCH_GALLERY_DIR");
    // A generator, not a check: with nowhere to write, there is nothing to run.
    if (dir == nullptr) {
        SKIP("HELIX_SWATCH_GALLERY_DIR is unset");
    }

    ScopedThemeMode mode;
    for (const bool dark : {false, true}) {
        mode.set(dark);
        lv_obj_t* page = lv_obj_create(test_screen());
        lv_obj_remove_style_all(page);
        lv_obj_set_size(page, 8 * 140, 3 * 140);

        int col = 0;
        for (const auto& f : FILAMENTS) {
            const auto finish = helix::ui::swatch_finish_from_variant(f.name);
            const int32_t x = col * 140 + 20;

            lv_obj_t* dot = lv_obj_create(page);
            lv_obj_remove_style_all(dot);
            lv_obj_set_size(dot, 64, 64);
            lv_obj_set_pos(dot, x + 18, 18);
            lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
            helix::ui::apply_swatch_color(dot, f.rgb, f.multi, finish);

            lv_obj_t* chip = lv_obj_create(page);
            lv_obj_remove_style_all(chip);
            lv_obj_set_size(chip, 100, 56);
            lv_obj_set_pos(chip, x, 112);
            lv_obj_set_style_radius(chip, theme_manager_get_spacing("border_radius_small"), 0);
            helix::ui::apply_swatch_color(chip, f.rgb, f.multi, finish);

            lv_obj_t* spool = ui_spool_canvas_create(page, 120);
            lv_obj_set_pos(spool, x - 10, 290);
            helix::ui::spool_canvas_set_filament(spool, f.rgb, f.multi, finish);
            ++col;
        }
        lv_obj_update_layout(page);
        lv_draw_buf_t* snap = lv_snapshot_take(page, LV_COLOR_FORMAT_ARGB8888);
        REQUIRE(snap != nullptr);
        write_png(std::string(dir) + (dark ? "/gallery-dark.png" : "/gallery-light.png"), snap,
                  theme_manager_get_color("screen_bg"));
        lv_draw_buf_destroy(snap);
        lv_obj_delete(page);
    }
}

// The spools alone, from the side and face-on, at a size where the winding,
// textures and vents can be judged:
//
//   HELIX_SWATCH_GALLERY_DIR=/tmp/out ./build/bin/helix-tests '[swatch_gallery_spools]'
TEST_CASE_METHOD(LVGLUITestFixture, "swatch gallery spools", "[.][swatch_gallery_spools]") {
    const char* dir = std::getenv("HELIX_SWATCH_GALLERY_DIR");
    // A generator, not a check: with nowhere to write, there is nothing to run.
    if (dir == nullptr) {
        SKIP("HELIX_SWATCH_GALLERY_DIR is unset");
    }
    const char* size_env = std::getenv("HELIX_SWATCH_GALLERY_SPOOL");
    const int32_t size = size_env ? std::atoi(size_env) : 240;
    REQUIRE(size > 0);

    ScopedThemeMode mode;
    for (const bool dark : {false, true}) {
        mode.set(dark);
        lv_obj_t* page = lv_obj_create(test_screen());
        lv_obj_remove_style_all(page);
        lv_obj_set_size(page, 8 * size, 3 * size);

        // Side view on the first row, face-on on the second, then one
        // filament from empty to full.
        for (const auto view : {helix::ui::SpoolView::Side, helix::ui::SpoolView::Front}) {
            const int32_t y = view == helix::ui::SpoolView::Side ? 0 : size;
            int col = 0;
            for (const auto& f : FILAMENTS) {
                lv_obj_t* spool = ui_spool_canvas_create(page, size);
                lv_obj_set_pos(spool, col * size, y);
                helix::ui::spool_canvas_set_view(spool, view);
                helix::ui::spool_canvas_set_filament(spool, f.rgb, f.multi,
                                                     helix::ui::swatch_finish_from_variant(f.name));
                ++col;
            }
        }
        int col = 0;
        for (const auto view : {helix::ui::SpoolView::Side, helix::ui::SpoolView::Front}) {
            for (const float fill : {0.0f, 0.02f, 0.3f, 1.0f}) {
                lv_obj_t* spool = ui_spool_canvas_create(page, size);
                lv_obj_set_pos(spool, col * size, 2 * size);
                helix::ui::spool_canvas_set_view(spool, view);
                ui_spool_canvas_set_color(spool, lv_color_hex(0x2080D0));
                ui_spool_canvas_set_fill_level(spool, fill);
                ++col;
            }
        }
        lv_obj_update_layout(page);
        lv_draw_buf_t* snap = lv_snapshot_take(page, LV_COLOR_FORMAT_ARGB8888);
        REQUIRE(snap != nullptr);
        write_png(std::string(dir) + (dark ? "/spools-dark.png" : "/spools-light.png"), snap,
                  theme_manager_get_color("screen_bg"));
        lv_draw_buf_destroy(snap);
        lv_obj_delete(page);
    }
}
