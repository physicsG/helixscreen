// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../lvgl_test_fixture.h"
#include "../test_helpers/gcode_layer_renderer_test_access.h"
#include "gcode_layer_renderer.h"
#include "gcode_parser.h"
#include "lodepng_encode.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "../catch_amalgamated.hpp"

using namespace helix::gcode;

// ---------------------------------------------------------------------------
// What crease shading costs, on real files. Hidden behind [.]: it prints
// numbers rather than asserting behaviour, and timings are machine-dependent.
//
//   ./build/bin/helix-tests "[crease_bench]"
//
// HELIX_CREASE_FILES   space-separated gcode paths (default: a set from
//                      assets/test_gcodes/)
// HELIX_CREASE_PNG_DIR when set, writes <name>.png per file: crease off on the
//                      left, on on the right, same view
// ---------------------------------------------------------------------------

namespace {

/// Canvas edge in pixels; HELIX_CREASE_PX overrides it.
int canvas_px() {
    const char* env = std::getenv("HELIX_CREASE_PX");
    const int px = env ? std::atoi(env) : 0;
    return px > 0 ? px : 480;
}
const int kPx = canvas_px();

std::unique_ptr<ParsedGCodeFile> parse(const std::string& path, double& ms) {
    const auto t0 = std::chrono::steady_clock::now();
    GCodeParser parser;
    std::ifstream f(path);
    std::string line;
    while (std::getline(f, line)) {
        parser.parse_line(line);
    }
    auto parsed = std::make_unique<ParsedGCodeFile>(parser.finalize());
    ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    return parsed;
}

struct Build {
    double ms = 0;
    size_t crease_bytes = 0;
    std::vector<uint32_t> pixels;
};

Build build_cache(const ParsedGCodeFile& gcode, bool crease, bool keep_pixels) {
    GCodeLayerRenderer r;
    r.set_gcode(&gcode);
    r.set_ghost_mode(false);
    r.set_crease_shading(crease);
    r.set_extrusion_color(lv_color_hex(0xE07B39));
    r.set_canvas_size(kPx, kPx);
    r.auto_fit();
    GCodeLayerRendererTestAccess::pin_layers_per_frame(r, 100000);

    const int last_layer = r.get_layer_count() - 1;
    Build b;
    const auto t0 = std::chrono::steady_clock::now();
    int done = -1;
    while (done < last_layer) {
        done = GCodeLayerRendererTestAccess::render_solid_batch(r, done + 1, last_layer, kPx, kPx);
    }
    b.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    b.crease_bytes = r.memory_report().bytes_for("crease_grids");

    if (keep_pixels) {
        GCodeLayerRendererTestAccess::apply_outline(r);
        b.pixels.resize(static_cast<size_t>(kPx) * kPx);
        for (int y = 0; y < kPx; ++y) {
            for (int x = 0; x < kPx; ++x) {
                b.pixels[static_cast<size_t>(y) * kPx + x] =
                    GCodeLayerRendererTestAccess::cache_pixel(r, x, y);
            }
        }
    }
    return b;
}

/// Off | on, side by side, composited over a dark panel background.
void write_pair(const std::string& path, const std::vector<uint32_t>& off,
                const std::vector<uint32_t>& on) {
    const int w = kPx * 2, h = kPx;
    std::vector<unsigned char> rgba(static_cast<size_t>(w) * h * 4);
    const int bg = 0x20;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const auto& src = x < kPx ? off : on;
            const uint32_t p = src[static_cast<size_t>(y) * kPx + (x % kPx)];
            const int a = static_cast<int>(p >> 24);
            unsigned char* d = &rgba[(static_cast<size_t>(y) * w + x) * 4];
            for (int c = 0; c < 3; ++c) {
                const int v = static_cast<int>((p >> (16 - 8 * c)) & 0xFF);
                d[c] = static_cast<unsigned char>((v * a + bg * (255 - a)) / 255);
            }
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

TEST_CASE_METHOD(LVGLTestFixture, "crease shading cost on real files", "[.][crease_bench]") {
    std::vector<std::string> files;
    if (const char* env = std::getenv("HELIX_CREASE_FILES")) {
        std::istringstream in(env);
        for (std::string f; in >> f;) {
            files.push_back(f);
        }
    } else {
        files = {"assets/test_gcodes/3DBenchy.gcode",
                 "assets/test_gcodes/Torture_Calibration_Cube_New.gcode",
                 "assets/test_gcodes/eiffel_final_PLA_2h42m.gcode",
                 "assets/test_gcodes/ECC_0.4_stand_PLA0.2_2h42m.gcode"};
    }
    const char* png_dir = std::getenv("HELIX_CREASE_PNG_DIR");

    std::printf("%-40s %8s %7s %9s %9s %7s %9s\n", "file", "segments", "parse", "build_off",
                "build_on", "delta", "grids_KB");
    for (const auto& path : files) {
        double parse_ms = 0;
        auto gcode = parse(path, parse_ms);
        REQUIRE(gcode->layers.size() > 0);

        // Best of three: the box is shared and the minimum is the least noisy.
        double off_ms = 1e30, on_ms = 1e30;
        size_t bytes = 0;
        for (int i = 0; i < 3; ++i) {
            off_ms = std::min(off_ms, build_cache(*gcode, false, false).ms);
            const Build on = build_cache(*gcode, true, false);
            on_ms = std::min(on_ms, on.ms);
            bytes = on.crease_bytes;
        }
        const std::string name = path.substr(path.find_last_of('/') + 1);
        std::printf("%-40s %8zu %6.0fms %7.1fms %7.1fms %+6.1f%% %9.1f\n", name.c_str(),
                    gcode->total_segments, parse_ms, off_ms, on_ms,
                    100.0 * (on_ms - off_ms) / off_ms, bytes / 1024.0);

        if (png_dir) {
            const Build off = build_cache(*gcode, false, true);
            const Build on = build_cache(*gcode, true, true);
            write_pair(std::string(png_dir) + "/" + name.substr(0, name.rfind('.')) + ".png",
                       off.pixels, on.pixels);
        }
    }
}
