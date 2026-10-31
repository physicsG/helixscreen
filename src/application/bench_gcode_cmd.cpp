// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "bench_gcode_cmd.h"

#include "gcode_layer_renderer.h"
#include "gcode_streaming_controller.h"
#include "stb_image_write.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>
#include <vector>

namespace helix::bench {

namespace {

/// Default ghost canvas: about the print-status preview on a CC1's 480x272 panel.
constexpr int kDefaultCanvasWidth = 320;
constexpr int kDefaultCanvasHeight = 240;
/// Single-layer parses timed from the middle of the file, where layers are typical.
constexpr size_t kSingleLayerSamples = 20;

struct Clock {
    double wall_s;
    double cpu_s;
};

double read_clock(clockid_t id) {
    timespec ts{};
    clock_gettime(id, &ts);
    return static_cast<double>(ts.tv_sec) + static_cast<double>(ts.tv_nsec) / 1e9;
}

Clock now() {
    return {read_clock(CLOCK_MONOTONIC), read_clock(CLOCK_PROCESS_CPUTIME_ID)};
}

Clock since(const Clock& start) {
    const Clock end = now();
    return {end.wall_s - start.wall_s, end.cpu_s - start.cpu_s};
}

void print_row(const char* name, const Clock& c, double mb) {
    const double mb_per_cpu_s = c.cpu_s > 0 ? mb / c.cpu_s : 0;
    std::printf("%-8s wall %8.2fs  cpu %8.2fs  %7.2f MB/cpu-s\n", name, c.wall_s, c.cpu_s,
                mb_per_cpu_s);
}

/// The ghost as the print-status screen shows it: blitted at 30% over a dark
/// card, so sampling choices can be compared by eye.
void write_ghost_png(const gcode::GCodeLayerRenderer& renderer, const char* path) {
    int w = 0;
    int h = 0;
    const std::vector<uint8_t> argb = renderer.copy_ghost_pixels(w, h);
    if (argb.empty()) {
        std::fprintf(stderr, "bench-gcode: no ghost to write\n");
        return;
    }
    constexpr int kBackground = 0x1e;
    constexpr int kGhostOpa = 77; // LV_OPA_30
    std::vector<uint8_t> rgb(static_cast<size_t>(w) * h * 3);
    for (size_t i = 0; i < static_cast<size_t>(w) * h; ++i) {
        const uint8_t* px = &argb[i * 4]; // B, G, R, A in memory
        const int a = px[3] * kGhostOpa / 255;
        for (int c = 0; c < 3; ++c) {
            rgb[i * 3 + c] = static_cast<uint8_t>((px[2 - c] * a + kBackground * (255 - a)) / 255);
        }
    }
    std::FILE* f = std::fopen(path, "wb");
    if (!f) {
        std::fprintf(stderr, "bench-gcode: could not open %s\n", path);
        return;
    }
    // The app's stb build has no stdio entry points; stream the PNG instead.
    auto sink = [](void* ctx, void* data, int size) {
        std::fwrite(data, 1, static_cast<size_t>(size), static_cast<std::FILE*>(ctx));
    };
    if (stbi_write_png_to_func(sink, f, w, h, 3, rgb.data(), w * 3) == 0) {
        std::fprintf(stderr, "bench-gcode: could not encode %s\n", path);
    }
    std::fclose(f);
}

} // namespace

int run_bench_gcode(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s bench-gcode <file.gcode> [width height]\n", argv[0]);
        return 2;
    }
    const std::string path = argv[2];
    const int width = argc >= 5 ? std::atoi(argv[3]) : kDefaultCanvasWidth;
    const int height = argc >= 5 ? std::atoi(argv[4]) : kDefaultCanvasHeight;
    spdlog::set_level(std::getenv("HELIX_BENCH_VERBOSE") ? spdlog::level::debug
                                                         : spdlog::level::warn);

    gcode::GCodeStreamingController controller;
    const Clock index_start = now();
    if (!controller.open_file(path)) {
        std::fprintf(stderr, "bench-gcode: could not index %s\n", path.c_str());
        return 1;
    }
    const Clock index = since(index_start);

    const double mb = static_cast<double>(controller.get_file_size()) / (1024.0 * 1024.0);
    const size_t layers = controller.get_layer_count();
    std::printf("file     %s\n", path.c_str());
    std::printf("size     %.1f MB, %zu layers, %zu lines\n", mb, layers,
                controller.get_index_stats().total_lines);
    print_row("index", index, mb);

    // The ghost exactly as the print-status preview builds it: the renderer's
    // own fit, sampling and rasterizer, on its own worker thread.
    const int repeat = std::getenv("HELIX_BENCH_REPEAT")
                           ? std::max(1, std::atoi(std::getenv("HELIX_BENCH_REPEAT")))
                           : 1;
    gcode::GCodeLayerRenderer renderer;
    renderer.set_canvas_size(width, height);
    // A filament orange: with no theme loaded the default color is near black,
    // which makes a written ghost image unreadable. Cost does not depend on it.
    renderer.set_extrusion_color(lv_color_hex(0xFF8A3D));
    renderer.set_streaming_controller(&controller);
    const Clock ghost_start = now();
    for (int i = 0; i < repeat; ++i) {
        // Every pass starts cold, as the first one in the UI does.
        for (size_t l = 0; l < layers; ++l) {
            controller.evict_layer(l);
        }
        renderer.build_ghost_blocking();
    }
    const Clock ghost = since(ghost_start);
    if (const char* png = std::getenv("HELIX_BENCH_GHOST_PNG")) {
        write_ghost_png(renderer, png);
    }
    const float layers_done = renderer.get_ghost_build_progress();
    std::printf("ghost    %dx%d canvas, %.0f%% complete\n", width, height, layers_done * 100.0f);
    print_row("ghost", ghost, mb * repeat);

    std::vector<double> layer_ms;
    const size_t first =
        layers / 2 > kSingleLayerSamples / 2 ? layers / 2 - kSingleLayerSamples / 2 : 0;
    for (size_t l = first; l < std::min(layers, first + kSingleLayerSamples); ++l) {
        controller.evict_layer(l);
        const Clock start = now();
        controller.load_layer_segments_no_prefetch(l);
        layer_ms.push_back(since(start).cpu_s * 1000.0);
    }
    if (!layer_ms.empty()) {
        std::sort(layer_ms.begin(), layer_ms.end());
        std::printf("layer    median %.1f ms cpu, max %.1f ms (%zu mid-file layers)\n",
                    layer_ms[layer_ms.size() / 2], layer_ms.back(), layer_ms.size());
    }
    return 0;
}

} // namespace helix::bench
