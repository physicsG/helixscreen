// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#if HELIX_ENABLE_SCREENSAVER

#include "../lvgl_test_fixture.h"
#include "../test_helpers/screensaver_sim_kit.h"
#include "../test_helpers/screensaver_test_access.h"
#include "screensaver_aqua.h"
#include "screensaver_fireflies.h"
#include "screensaver_fireworks.h"
#include "screensaver_flip_clock.h"
#include "screensaver_highfly.h"
#include "screensaver_hyperspace.h"
#include "screensaver_maze.h"
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

// Pixel fingerprints of every canvas saver. A fixed seed, a fixed resolution and fixed frame
// times make a run draw the same pixels every time, so any change to a single drawn pixel
// changes the hash. The recorded values live in kFingerprintFile; with
// HELIX_SCREENSAVER_FINGERPRINT_WRITE set, a test records the value it computes and fails, so
// a recording run can never pass as a comparison.

using helix::ui::AquaScreensaver;
using helix::ui::ClockInput;
using helix::ui::ClockPrintPhase;
using helix::ui::FirefliesScreensaver;
using helix::ui::FireworksScreensaver;
using helix::ui::FlipClockScreensaver;
using helix::ui::HighFlyScreensaver;
using helix::ui::HyperspaceScreensaver;
using helix::ui::MazeArtSources;
using helix::ui::MazeScreensaver;

namespace {

constexpr const char* kFingerprintFile = "tests/fixtures/screensaver/fingerprints.txt";
using helix::test::fnv1a;
using helix::test::frame_fnv1a;
using helix::test::frame_of;
using helix::test::kFnv1aOffset;
using helix::test::kUnevenFrameMs;

/// Hash of every pixel byte of the canvas, row by row, leaving out any stride padding.
uint64_t canvas_hash(lv_obj_t* canvas) {
    REQUIRE(canvas != nullptr);
    return frame_fnv1a(frame_of(lv_canvas_get_draw_buf(canvas)));
}

/// Folds one frame's hash into the run's.
uint64_t fold(uint64_t run, uint64_t frame) {
    uint8_t bytes[8];
    for (int i = 0; i < 8; i++) {
        bytes[i] = static_cast<uint8_t>(frame >> (8 * i));
    }
    return fnv1a(run, bytes, sizeof(bytes));
}

constexpr uint32_t kSteadyMs[] = {100};

/// What a clock saver reads during a run: 10:08:30 when it starts, the clock moving with the
/// run, and a print with an hour and a bit left when `printing` is set.
ClockInput clock_input_at(uint32_t elapsed_ms, bool printing) {
    ClockInput in;
    in.seconds_of_day = 10 * 3600 + 8 * 60 + 30 + static_cast<int32_t>(elapsed_ms / 1000);
    in.date = {2026, 10, 16, 5};
    if (printing) {
        in.print.phase = ClockPrintPhase::PRINTING;
        in.print.remaining_s = 3723;
        in.print.progress_pct = 40;
    }
    return in;
}

/**
 * @brief Hash of a run: the canvas after start(), then after every `sample_every`th frame
 *
 * `ss` starts with `seed` at 800x480 and runs `frames` frames, frame n taking
 * frame_ms[n % frame_ms.size()]. `elapsed_ms` follows the run's clock, for input sources.
 */
template <typename Saver, size_t N>
uint64_t saver_run(Saver& ss, uint32_t seed, int frames, int sample_every,
                   const uint32_t (&frame_ms)[N], uint32_t* elapsed_ms = nullptr) {
    ScopedResolution resolution(lv_display_get_default(), 800, 480);
    ScreensaverStopOnExit<Saver> stop_on_exit{ss};
    SaverTestAccess::set_fixed_seed(ss, seed);
    ss.start();
    REQUIRE(ss.is_active());
    uint64_t run = fold(kFnv1aOffset, canvas_hash(SaverTestAccess::canvas(ss)));
    for (int frame = 1; frame <= frames; frame++) {
        const uint32_t ms = frame_ms[static_cast<size_t>(frame) % N];
        if (elapsed_ms != nullptr) {
            *elapsed_ms += ms;
        }
        advance_saver(ss, ms);
        if (frame % sample_every == 0) {
            run = fold(run, canvas_hash(SaverTestAccess::canvas(ss)));
        }
    }
    return run;
}

uint64_t pipes_run(uint32_t seed) {
    PipesScreensaver ss;
    return saver_run(ss, seed, 120, 20, kSteadyMs);
}

uint64_t starfield_run(uint32_t seed) {
    StarfieldScreensaver ss;
    return saver_run(ss, seed, 150, 25, kUnevenFrameMs);
}

uint64_t fireworks_run(uint32_t seed) {
    FireworksScreensaver ss;
    return saver_run(ss, seed, 150, 25, kUnevenFrameMs);
}

uint64_t hyperspace_run(uint32_t seed) {
    HyperspaceScreensaver ss;
    return saver_run(ss, seed, 120, 20, kUnevenFrameMs);
}

uint64_t fireflies_run(uint32_t seed) {
    FirefliesScreensaver ss;
    return saver_run(ss, seed, 150, 25, kUnevenFrameMs);
}

uint64_t maze_run(uint32_t seed) {
    MazeScreensaver ss;
    // The logo only: app art depends on what the test process happens to hold.
    ss.set_art_source([] { return MazeArtSources{}; });
    return saver_run(ss, seed, 120, 20, kUnevenFrameMs);
}

/// The flip clock draws no random numbers, so a different start time stands in for a seed. A
/// 25 s offset starts the run at 10:08:55, so its cards flip to 10:09 five seconds in.
uint64_t flip_clock_run(uint32_t start_offset_s, bool printing) {
    FlipClockScreensaver ss;
    uint32_t elapsed_ms = start_offset_s * 1000;
    ss.set_input_source([&elapsed_ms, printing] { return clock_input_at(elapsed_ms, printing); });
    return saver_run(ss, 1, 120, 20, kSteadyMs, &elapsed_ms);
}

uint64_t aqua_run(uint32_t seed, bool printing) {
    AquaScreensaver ss;
    uint32_t elapsed_ms = 0;
    ss.set_input_source([&elapsed_ms, printing] { return clock_input_at(elapsed_ms, printing); });
    return saver_run(ss, seed, 90, 15, kUnevenFrameMs, &elapsed_ms);
}

uint64_t highfly_run(uint32_t seed, bool printing) {
    HighFlyScreensaver ss;
    uint32_t elapsed_ms = 0;
    ss.set_input_source([&elapsed_ms, printing] { return clock_input_at(elapsed_ms, printing); });
    return saver_run(ss, seed, 90, 15, kUnevenFrameMs, &elapsed_ms);
}

std::string hex(uint64_t value) {
    char text[17];
    std::snprintf(text, sizeof(text), "%016" PRIx64, value);
    return text;
}

std::map<std::string, std::string> read_fingerprints() {
    std::map<std::string, std::string> values;
    std::ifstream file(kFingerprintFile);
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') {
            continue;
        }
        std::istringstream fields(line);
        std::string name;
        std::string value;
        std::string accepted;
        if (fields >> name) {
            while (fields >> value) {
                accepted += accepted.empty() ? value : " " + value;
            }
            values[name] = accepted;
        }
    }
    return values;
}

bool accepts(const std::string& accepted, const std::string& value) {
    std::istringstream tokens(accepted);
    std::string token;
    while (tokens >> token) {
        if (token == value) {
            return true;
        }
    }
    return false;
}

void write_fingerprints(const std::map<std::string, std::string>& values) {
    std::ofstream file(kFingerprintFile, std::ios::trunc);
    file << "# Canvas pixel fingerprints; tests/unit/test_screensaver_fingerprint.cpp\n";
    for (const auto& [name, value] : values) {
        file << name << ' ' << value << '\n';
    }
}

/// Fingerprints are recorded with the Linux x86-64 test builds; another compiler or target may
/// contract floating point differently and draw a pixel one step apart. A line may list one
/// value per toolchain that draws the saver differently (clang on thelio, g++ in CI's image).
void skip_off_the_recording_platform() {
#if !(defined(__linux__) && defined(__x86_64__))
    SKIP("screensaver fingerprints are recorded on x86-64 Linux");
#endif
}

void check_fingerprint(const std::string& name, uint64_t value) {
    if (std::getenv("HELIX_SCREENSAVER_FINGERPRINT_WRITE") != nullptr) {
        std::map<std::string, std::string> values = read_fingerprints();
        std::string& accepted = values[name];
        if (!accepts(accepted, hex(value))) {
            accepted += accepted.empty() ? hex(value) : " " + hex(value);
        }
        write_fingerprints(values);
        FAIL("recorded " << name << " " << hex(value) << " in " << kFingerprintFile
                         << "; unset HELIX_SCREENSAVER_FINGERPRINT_WRITE to compare");
    }
    const std::map<std::string, std::string> recorded = read_fingerprints();
    INFO("fingerprint file " << kFingerprintFile << " (run helix-tests from the repo root)");
    REQUIRE(recorded.count(name) == 1);
    INFO("accepted " << recorded.at(name));
    CHECK(accepts(recorded.at(name), hex(value)));
}

} // namespace

TEST_CASE_METHOD(LVGLTestFixture, "every canvas saver matches its recorded pixel fingerprint",
                 "[screensaver][screensaver_fingerprint]") {
    skip_off_the_recording_platform();
    SECTION("pipes") {
        check_fingerprint("pipes", pipes_run(42));
    }
    SECTION("starfield") {
        check_fingerprint("starfield", starfield_run(5));
    }
    SECTION("fireworks") {
        check_fingerprint("fireworks", fireworks_run(7));
    }
    SECTION("hyperspace") {
        check_fingerprint("hyperspace", hyperspace_run(11));
    }
    SECTION("fireflies") {
        check_fingerprint("fireflies", fireflies_run(13));
    }
    SECTION("maze") {
        check_fingerprint("maze", maze_run(17));
    }
    SECTION("flip clock") {
        check_fingerprint("flip_clock", flip_clock_run(25, false));
    }
    SECTION("flip clock counting a print down") {
        check_fingerprint("flip_clock_print", flip_clock_run(25, true));
    }
    SECTION("aqua") {
        check_fingerprint("aqua", aqua_run(19, false));
    }
    SECTION("aqua counting a print down") {
        check_fingerprint("aqua_print", aqua_run(19, true));
    }
    SECTION("high fly") {
        check_fingerprint("highfly", highfly_run(23, false));
    }
    SECTION("high fly counting a print down") {
        check_fingerprint("highfly_print", highfly_run(23, true));
    }
}

TEST_CASE_METHOD(LVGLTestFixture,
                 "a fingerprint run replays exactly and a different seed draws different pixels",
                 "[screensaver][screensaver_fingerprint]") {
    SECTION("pipes") {
        CHECK(pipes_run(42) == pipes_run(42));
        CHECK(pipes_run(42) != pipes_run(43));
    }
    SECTION("starfield") {
        CHECK(starfield_run(5) == starfield_run(5));
        CHECK(starfield_run(5) != starfield_run(6));
    }
    SECTION("fireworks") {
        CHECK(fireworks_run(7) == fireworks_run(7));
        CHECK(fireworks_run(7) != fireworks_run(8));
    }
    SECTION("hyperspace") {
        CHECK(hyperspace_run(11) == hyperspace_run(11));
        CHECK(hyperspace_run(11) != hyperspace_run(12));
    }
    SECTION("fireflies") {
        CHECK(fireflies_run(13) == fireflies_run(13));
        CHECK(fireflies_run(13) != fireflies_run(14));
    }
    SECTION("maze") {
        CHECK(maze_run(17) == maze_run(17));
        CHECK(maze_run(17) != maze_run(18));
    }
    SECTION("flip clock") {
        CHECK(flip_clock_run(25, false) == flip_clock_run(25, false));
        CHECK(flip_clock_run(25, false) != flip_clock_run(85, false));
        CHECK(flip_clock_run(25, false) != flip_clock_run(25, true));
    }
    SECTION("aqua") {
        CHECK(aqua_run(19, false) == aqua_run(19, false));
        CHECK(aqua_run(19, false) != aqua_run(20, false));
        CHECK(aqua_run(19, false) != aqua_run(19, true));
    }
    SECTION("high fly") {
        CHECK(highfly_run(23, false) == highfly_run(23, false));
        CHECK(highfly_run(23, false) != highfly_run(24, false));
        CHECK(highfly_run(23, false) != highfly_run(23, true));
    }
}

#endif // HELIX_ENABLE_SCREENSAVER
