// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#ifdef HELIX_ENABLE_SCREENSAVER

#include "../lvgl_test_fixture.h"
#include "../test_helpers/screensaver_test_access.h"
#include "screensaver_pipes.h"
#include "screensaver_starfield.h"

#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

#include "../catch_amalgamated.hpp"

// Pixel fingerprints of the canvas savers. A fixed seed, a fixed resolution and fixed frame
// times make a run draw the same pixels every time, so any change to a single drawn pixel
// changes the hash. The recorded values live in FINGERPRINT_FILE; with
// HELIX_SCREENSAVER_FINGERPRINT_WRITE set, a test records the value it computes and fails, so
// a recording run can never pass as a comparison.

namespace {

constexpr const char* FINGERPRINT_FILE = "tests/fixtures/screensaver/fingerprints.txt";
constexpr uint64_t FNV_OFFSET = 14695981039346656037ULL;
constexpr uint64_t FNV_PRIME = 1099511628211ULL;

// The saver seams: the only lines that change when a saver moves to another test access class.
void seed_pipes(PipesScreensaver& ss, uint32_t seed) {
    SaverTestAccess::set_fixed_seed(ss, seed);
}
lv_timer_t* pipes_timer(const PipesScreensaver& ss) {
    return SaverTestAccess::timer(ss);
}
lv_obj_t* pipes_canvas(const PipesScreensaver& ss) {
    return SaverTestAccess::canvas(ss);
}
void seed_starfield(StarfieldScreensaver& ss, uint32_t seed) {
    SaverTestAccess::set_fixed_seed(ss, seed);
}
lv_timer_t* starfield_timer(const StarfieldScreensaver& ss) {
    return SaverTestAccess::timer(ss);
}
lv_obj_t* starfield_canvas(const StarfieldScreensaver& ss) {
    return SaverTestAccess::canvas(ss);
}

uint64_t fnv1a(uint64_t hash, const uint8_t* data, size_t size) {
    for (size_t i = 0; i < size; i++) {
        hash ^= data[i];
        hash *= FNV_PRIME;
    }
    return hash;
}

/// Hash of every pixel byte of the canvas, row by row, leaving out any stride padding.
uint64_t canvas_hash(lv_obj_t* canvas) {
    REQUIRE(canvas != nullptr);
    const lv_draw_buf_t* buf = lv_canvas_get_draw_buf(canvas);
    REQUIRE(buf != nullptr);
    const uint32_t row_bytes =
        buf->header.w * lv_color_format_get_size(static_cast<lv_color_format_t>(buf->header.cf));
    uint64_t hash = FNV_OFFSET;
    for (uint32_t y = 0; y < buf->header.h; y++) {
        hash = fnv1a(hash, buf->data + static_cast<size_t>(y) * buf->header.stride, row_bytes);
    }
    return hash;
}

/// Folds one frame's hash into the run's.
uint64_t fold(uint64_t run, uint64_t frame) {
    uint8_t bytes[8];
    for (int i = 0; i < 8; i++) {
        bytes[i] = static_cast<uint8_t>(frame >> (8 * i));
    }
    return fnv1a(run, bytes, sizeof(bytes));
}

void fire(lv_timer_t* timer) {
    REQUIRE(timer != nullptr);
    REQUIRE(timer->timer_cb != nullptr);
    timer->timer_cb(timer);
}

uint64_t pipes_run(uint32_t seed) {
    ScopedResolution resolution(lv_display_get_default(), 800, 480);
    PipesScreensaver ss;
    ScreensaverStopOnExit<PipesScreensaver> stop_on_exit{ss};
    seed_pipes(ss, seed);
    ss.start();
    REQUIRE(ss.is_active());
    uint64_t run = fold(FNV_OFFSET, canvas_hash(pipes_canvas(ss)));
    for (int frame = 1; frame <= 120; frame++) {
        if (frame == 61) {
            // A full grid makes this frame reset the scene under a new camera.
            PipesScreensaverTestAccess::set_total_segments(
                ss, PipesScreensaverTestAccess::max_segments() + 1);
        }
        lv_tick_inc(100);
        fire(pipes_timer(ss));
        if (frame % 20 == 0) {
            run = fold(run, canvas_hash(pipes_canvas(ss)));
        }
    }
    return run;
}

uint64_t starfield_run(uint32_t seed) {
    ScopedResolution resolution(lv_display_get_default(), 800, 480);
    StarfieldScreensaver ss;
    ScreensaverStopOnExit<StarfieldScreensaver> stop_on_exit{ss};
    seed_starfield(ss, seed);
    ss.start();
    REQUIRE(ss.is_active());
    uint64_t run = fold(FNV_OFFSET, canvas_hash(starfield_canvas(ss)));
    constexpr uint32_t FRAME_MS[] = {33, 16, 50, 7, 70};
    for (int frame = 1; frame <= 150; frame++) {
        lv_tick_inc(FRAME_MS[frame % 5]);
        fire(starfield_timer(ss));
        if (frame % 25 == 0) {
            run = fold(run, canvas_hash(starfield_canvas(ss)));
        }
    }
    return run;
}

std::string hex(uint64_t value) {
    char text[17];
    std::snprintf(text, sizeof(text), "%016" PRIx64, value);
    return text;
}

std::map<std::string, std::string> read_fingerprints() {
    std::map<std::string, std::string> values;
    std::ifstream file(FINGERPRINT_FILE);
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') {
            continue;
        }
        std::istringstream fields(line);
        std::string name;
        std::string value;
        if (fields >> name >> value) {
            values[name] = value;
        }
    }
    return values;
}

void write_fingerprints(const std::map<std::string, std::string>& values) {
    std::ofstream file(FINGERPRINT_FILE, std::ios::trunc);
    file << "# Canvas pixel fingerprints; tests/unit/test_screensaver_fingerprint.cpp\n";
    for (const auto& [name, value] : values) {
        file << name << ' ' << value << '\n';
    }
}

/// Fingerprints are recorded with the Linux x86-64 test build; another compiler or target may
/// contract floating point differently and draw a pixel one step apart.
void skip_off_the_recording_platform() {
#if !(defined(__linux__) && defined(__x86_64__))
    SKIP("screensaver fingerprints are recorded on x86-64 Linux");
#endif
}

void check_fingerprint(const std::string& name, uint64_t value) {
    if (std::getenv("HELIX_SCREENSAVER_FINGERPRINT_WRITE") != nullptr) {
        std::map<std::string, std::string> values = read_fingerprints();
        values[name] = hex(value);
        write_fingerprints(values);
        FAIL("recorded " << name << " " << hex(value) << " in " << FINGERPRINT_FILE
                         << "; unset HELIX_SCREENSAVER_FINGERPRINT_WRITE to compare");
    }
    const std::map<std::string, std::string> recorded = read_fingerprints();
    INFO("fingerprint file " << FINGERPRINT_FILE << " (run helix-tests from the repo root)");
    REQUIRE(recorded.count(name) == 1);
    CHECK(hex(value) == recorded.at(name));
}

} // namespace

TEST_CASE_METHOD(LVGLTestFixture, "the pipes canvas matches its recorded pixel fingerprint",
                 "[screensaver][screensaver_fingerprint]") {
    skip_off_the_recording_platform();
    check_fingerprint("pipes", pipes_run(42));
}

TEST_CASE_METHOD(LVGLTestFixture, "the starfield canvas matches its recorded pixel fingerprint",
                 "[screensaver][screensaver_fingerprint]") {
    skip_off_the_recording_platform();
    check_fingerprint("starfield", starfield_run(5));
}

TEST_CASE_METHOD(LVGLTestFixture,
                 "a fingerprint run replays exactly and a different seed draws different pixels",
                 "[screensaver][screensaver_fingerprint]") {
    CHECK(pipes_run(42) == pipes_run(42));
    CHECK(pipes_run(42) != pipes_run(43));
    CHECK(starfield_run(5) == starfield_run(5));
    CHECK(starfield_run(5) != starfield_run(6));
}

#endif // HELIX_ENABLE_SCREENSAVER
