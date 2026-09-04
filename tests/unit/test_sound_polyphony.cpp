// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "sound_backend.h"
#include "sound_theme.h"

#include <cmath>

#include "../catch_amalgamated.hpp"

using Catch::Approx;

// ============================================================================
// MockBackend — minimal SoundBackend for voice interface testing
// ============================================================================

// Anonymous namespace gives this MockBackend internal linkage so it cannot be
// merged with the differently-laid-out MockBackend in test_sound_sequencer.cpp
// (an ODR violation that corrupted that test's std::mutex — see the note there).
namespace {

class MockBackend : public SoundBackend {
  public:
    int tone_calls = 0;
    float last_freq = 0, last_amp = 0, last_duty = 0;
    int silence_calls = 0;

    void set_tone(float f, float a, float d) override {
        tone_calls++;
        last_freq = f;
        last_amp = a;
        last_duty = d;
    }
    void silence() override {
        silence_calls++;
    }
};

} // namespace

// ============================================================================
// Helper: load a theme from a JSON string via a temp file
// ============================================================================

static std::optional<SoundTheme> load_test_theme(const std::string& json_str) {
    // SoundThemeParser::load_from_string() exists — use it directly
    return SoundThemeParser::load_from_string(json_str);
}

// ============================================================================
// Chord parsing tests
// ============================================================================

TEST_CASE("Chord parsing — 3 notes", "[sound][polyphony]") {
    const std::string json = R"({
        "name": "test", "version": 1,
        "sounds": {
            "chord_test": {
                "steps": [
                    { "notes": ["C4", "E4", "G4"], "dur": 100 }
                ]
            }
        }
    })";

    auto theme = load_test_theme(json);
    REQUIRE(theme.has_value());
    REQUIRE(theme->sounds.count("chord_test") == 1);

    const auto& steps = theme->sounds.at("chord_test").steps;
    REQUIRE(steps.size() == 1);

    const auto& step = steps[0];
    REQUIRE(step.chord_count == 3);
    CHECK(step.chord_freqs[0] == Approx(261.63f).epsilon(0.01));
    CHECK(step.chord_freqs[1] == Approx(329.63f).epsilon(0.01));
    CHECK(step.chord_freqs[2] == Approx(392.00f).epsilon(0.01));
}

TEST_CASE("Chord parsing — single note fallback", "[sound][polyphony]") {
    const std::string json = R"({
        "name": "test", "version": 1,
        "sounds": {
            "mono_test": {
                "steps": [
                    { "note": "A4", "dur": 100 }
                ]
            }
        }
    })";

    auto theme = load_test_theme(json);
    REQUIRE(theme.has_value());
    REQUIRE(theme->sounds.count("mono_test") == 1);

    const auto& step = theme->sounds.at("mono_test").steps[0];
    CHECK(step.chord_count == 0);
    CHECK(step.freq_hz == Approx(440.0f).epsilon(0.01));
}

TEST_CASE("Chord parsing — capped to 4", "[sound][polyphony]") {
    const std::string json = R"({
        "name": "test", "version": 1,
        "sounds": {
            "five_notes": {
                "steps": [
                    { "notes": ["C4","D4","E4","F4","G4"], "dur": 100 }
                ]
            }
        }
    })";

    auto theme = load_test_theme(json);
    REQUIRE(theme.has_value());
    REQUIRE(theme->sounds.count("five_notes") == 1);

    const auto& step = theme->sounds.at("five_notes").steps[0];
    CHECK(step.chord_count == 4);
}

TEST_CASE("Chord parsing — notes overrides note", "[sound][polyphony]") {
    const std::string json = R"({
        "name": "test", "version": 1,
        "sounds": {
            "override_test": {
                "steps": [
                    { "note": "A4", "notes": ["C4","E4"], "dur": 100 }
                ]
            }
        }
    })";

    auto theme = load_test_theme(json);
    REQUIRE(theme.has_value());
    REQUIRE(theme->sounds.count("override_test") == 1);

    const auto& step = theme->sounds.at("override_test").steps[0];
    CHECK(step.chord_count == 2);
    // Root note for mono fallback should be C4
    CHECK(step.freq_hz == Approx(261.63f).epsilon(0.01));
}

// ============================================================================
// Backend voice interface tests
// ============================================================================

TEST_CASE("Backend voice_count default", "[sound][polyphony]") {
    MockBackend backend;
    CHECK(backend.voice_count() == 1);
}

TEST_CASE("Backend set_voice default — slot 0 delegates to set_tone", "[sound][polyphony]") {
    MockBackend backend;
    backend.set_voice(0, 440.0f, 0.8f, 0.5f);

    CHECK(backend.tone_calls == 1);
    CHECK(backend.last_freq == Approx(440.0f));
    CHECK(backend.last_amp == Approx(0.8f));
    CHECK(backend.last_duty == Approx(0.5f));
}

TEST_CASE("Backend set_voice default — slot 1+ is no-op", "[sound][polyphony]") {
    MockBackend backend;
    backend.set_voice(1, 440.0f, 0.8f, 0.5f);

    CHECK(backend.tone_calls == 0);
}
