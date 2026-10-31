// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "sound_theme.h"

#include "../catch_amalgamated.hpp"

using Catch::Approx;

// ============================================================================
// Helper: minimal valid theme JSON
// ============================================================================

static const char* kMinimalThemeJson = R"({
    "name": "test-theme",
    "description": "A test theme",
    "version": 1,
    "sounds": {}
})";

static const char* kCompleteThemeJson = R"({
    "name": "complete-theme",
    "description": "Full-featured test theme",
    "version": 2,
    "defaults": {
        "wave": "triangle",
        "vel": 0.7,
        "env": { "a": 10, "d": 50, "s": 0.5, "r": 100 }
    },
    "sounds": {
        "button_tap": {
            "description": "Crisp click",
            "steps": [
                { "freq": 4000, "dur": 6, "wave": "square", "vel": 0.9,
                  "env": { "a": 1, "d": 5, "s": 0, "r": 1 } }
            ]
        },
        "toggle_on": {
            "description": "Two-tone confirm",
            "steps": [
                { "freq": 1200, "dur": 30 },
                { "freq": 1800, "dur": 40 }
            ]
        },
        "print_complete": {
            "description": "Triumphant arpeggio",
            "bpm": 140,
            "steps": [
                { "note": "C5", "dur": "8n", "wave": "square", "vel": 0.8 },
                { "note": "E5", "dur": "8n" },
                { "note": "G5", "dur": "8n" },
                { "note": "C6", "dur": "4n", "vel": 1.0 }
            ]
        },
        "error_alert": {
            "description": "Pulsing alert",
            "steps": [
                { "freq": 2400, "dur": 150, "wave": "saw",
                  "lfo": { "target": "amplitude", "rate": 8, "depth": 0.5 },
                  "env": { "a": 2, "d": 20, "s": 0.9, "r": 30 } },
                { "pause": 80 },
                { "freq": 2400, "dur": 150 }
            ],
            "repeat": 3
        },
        "nav_forward": {
            "description": "Ascending chirp with filter sweep",
            "steps": [
                { "freq": 600, "dur": 50, "wave": "saw",
                  "sweep": { "target": "freq", "end": 2400 },
                  "filter": { "type": "lowpass", "cutoff": 800, "sweep_to": 4000 } }
            ]
        }
    }
})";

// ============================================================================
// 1. Parse valid complete theme JSON
// ============================================================================

TEST_CASE("SoundTheme: parse valid complete theme", "[sound][theme]") {
    auto theme = SoundThemeParser::load_from_string(kCompleteThemeJson);
    REQUIRE(theme.has_value());

    CHECK(theme->name == "complete-theme");
    CHECK(theme->description == "Full-featured test theme");
    CHECK(theme->version == 2);
    CHECK(theme->sounds.size() == 5);

    // Check defaults were parsed
    CHECK(theme->default_wave == Waveform::TRIANGLE);
    CHECK(theme->default_velocity == Approx(0.7f));
    CHECK(theme->default_envelope.attack_ms == Approx(10));
    CHECK(theme->default_envelope.decay_ms == Approx(50));
    CHECK(theme->default_envelope.sustain_level == Approx(0.5f));
    CHECK(theme->default_envelope.release_ms == Approx(100));

    // Verify specific sound loaded
    REQUIRE(theme->sounds.count("button_tap") == 1);
    auto& tap = theme->sounds.at("button_tap");
    CHECK(tap.description == "Crisp click");
    CHECK(tap.steps.size() == 1);
    CHECK(tap.steps[0].freq_hz == Approx(4000));
    CHECK(tap.steps[0].duration_ms == Approx(6));
    CHECK(tap.steps[0].wave == Waveform::SQUARE);
    CHECK(tap.steps[0].velocity == Approx(0.9f));

    // Verify multi-step sound
    REQUIRE(theme->sounds.count("toggle_on") == 1);
    auto& toggle = theme->sounds.at("toggle_on");
    CHECK(toggle.steps.size() == 2);
    CHECK(toggle.steps[0].freq_hz == Approx(1200));
    CHECK(toggle.steps[1].freq_hz == Approx(1800));
}

// ============================================================================
// 2. Note name to frequency
// ============================================================================

TEST_CASE("SoundTheme: note_to_freq basic notes", "[sound][theme]") {
    // A4 = 440 Hz (concert pitch, the reference)
    CHECK(SoundThemeParser::note_to_freq("A4") == Approx(440.0f).epsilon(0.01));

    // C4 = 261.63 Hz (middle C)
    CHECK(SoundThemeParser::note_to_freq("C4") == Approx(261.63f).epsilon(0.01));

    // C5 = 523.25 Hz (one octave above middle C)
    CHECK(SoundThemeParser::note_to_freq("C5") == Approx(523.25f).epsilon(0.01));
}

TEST_CASE("SoundTheme: note_to_freq sharps and flats", "[sound][theme]") {
    float c_sharp = SoundThemeParser::note_to_freq("C#4");
    float d_flat = SoundThemeParser::note_to_freq("Db4");

    // C#4 and Db4 are enharmonic — same frequency
    CHECK(c_sharp == Approx(d_flat).epsilon(0.01));
    CHECK(c_sharp == Approx(277.18f).epsilon(0.01));

    // F#4
    CHECK(SoundThemeParser::note_to_freq("F#4") == Approx(369.99f).epsilon(0.01));

    // Bb4
    CHECK(SoundThemeParser::note_to_freq("Bb4") == Approx(466.16f).epsilon(0.01));
}

TEST_CASE("SoundTheme: note_to_freq octave range", "[sound][theme]") {
    // A across all octaves — each octave doubles the frequency
    float a0 = SoundThemeParser::note_to_freq("A0");
    float a1 = SoundThemeParser::note_to_freq("A1");
    float a2 = SoundThemeParser::note_to_freq("A2");
    float a3 = SoundThemeParser::note_to_freq("A3");
    float a8 = SoundThemeParser::note_to_freq("A8");

    CHECK(a0 == Approx(27.5f).epsilon(0.01));
    CHECK(a1 == Approx(55.0f).epsilon(0.01));
    CHECK(a2 == Approx(110.0f).epsilon(0.01));
    CHECK(a3 == Approx(220.0f).epsilon(0.01));
    CHECK(a8 == Approx(7040.0f).epsilon(0.01));

    // Each octave is 2x the previous
    CHECK(a1 == Approx(a0 * 2).epsilon(0.01));
    CHECK(a2 == Approx(a1 * 2).epsilon(0.01));
}

TEST_CASE("SoundTheme: note_to_freq invalid notes", "[sound][theme]") {
    CHECK(SoundThemeParser::note_to_freq("") == Approx(0.0f));
    CHECK(SoundThemeParser::note_to_freq("X4") == Approx(0.0f));
    CHECK(SoundThemeParser::note_to_freq("C") == Approx(0.0f));
    CHECK(SoundThemeParser::note_to_freq("C9") == Approx(0.0f));
    CHECK(SoundThemeParser::note_to_freq("H4") == Approx(0.0f));
}

// ============================================================================
// 3. Musical durations at 120 BPM
// ============================================================================

TEST_CASE("SoundTheme: musical_duration_to_ms at 120 BPM", "[sound][theme]") {
    const float bpm = 120.0f;
    // At 120 BPM, quarter note = 500ms

    CHECK(SoundThemeParser::musical_duration_to_ms("4n", bpm) == Approx(500.0f).epsilon(0.01));
    CHECK(SoundThemeParser::musical_duration_to_ms("8n", bpm) == Approx(250.0f).epsilon(0.01));
    CHECK(SoundThemeParser::musical_duration_to_ms("16n", bpm) == Approx(125.0f).epsilon(0.01));
    CHECK(SoundThemeParser::musical_duration_to_ms("2n", bpm) == Approx(1000.0f).epsilon(0.01));
    CHECK(SoundThemeParser::musical_duration_to_ms("1n", bpm) == Approx(2000.0f).epsilon(0.01));
}

TEST_CASE("SoundTheme: dotted and triplet durations", "[sound][theme]") {
    const float bpm = 120.0f;

    // Dotted quarter = quarter * 1.5 = 750ms at 120 BPM
    CHECK(SoundThemeParser::musical_duration_to_ms("4n.", bpm) == Approx(750.0f).epsilon(0.01));

    // Eighth triplet = quarter / 3 = ~166.67ms at 120 BPM
    CHECK(SoundThemeParser::musical_duration_to_ms("8t", bpm) == Approx(166.67f).epsilon(0.1));
}

TEST_CASE("SoundTheme: invalid musical durations", "[sound][theme]") {
    CHECK(SoundThemeParser::musical_duration_to_ms("", 120) == Approx(0.0f));
    CHECK(SoundThemeParser::musical_duration_to_ms("xyz", 120) == Approx(0.0f));
    CHECK(SoundThemeParser::musical_duration_to_ms("4n", 0) == Approx(0.0f)); // 0 BPM = invalid
}

// ============================================================================
// 4. ADSR envelope parsing with all fields
// ============================================================================

TEST_CASE("SoundTheme: ADSR envelope parsing", "[sound][theme]") {
    auto theme = SoundThemeParser::load_from_string(kCompleteThemeJson);
    REQUIRE(theme.has_value());

    auto& tap = theme->sounds.at("button_tap");
    auto& env = tap.steps[0].envelope;

    CHECK(env.attack_ms == Approx(1));
    CHECK(env.decay_ms == Approx(5));
    CHECK(env.sustain_level == Approx(0));
    CHECK(env.release_ms == Approx(1));
}

// ============================================================================
// 5. ADSR defaults when fields omitted
// ============================================================================

TEST_CASE("SoundTheme: ADSR defaults from theme defaults", "[sound][theme]") {
    auto theme = SoundThemeParser::load_from_string(kCompleteThemeJson);
    REQUIRE(theme.has_value());

    // toggle_on steps don't specify envelope — should get theme defaults
    auto& toggle = theme->sounds.at("toggle_on");
    auto& env = toggle.steps[0].envelope;

    CHECK(env.attack_ms == Approx(10));
    CHECK(env.decay_ms == Approx(50));
    CHECK(env.sustain_level == Approx(0.5f));
    CHECK(env.release_ms == Approx(100));
}

// ============================================================================
// 6. LFO parsing
// ============================================================================

TEST_CASE("SoundTheme: LFO parsing with target/rate/depth", "[sound][theme]") {
    auto theme = SoundThemeParser::load_from_string(kCompleteThemeJson);
    REQUIRE(theme.has_value());

    auto& alert = theme->sounds.at("error_alert");
    auto& lfo = alert.steps[0].lfo;

    CHECK(lfo.target == "amplitude");
    CHECK(lfo.rate == Approx(8));
    CHECK(lfo.depth == Approx(0.5f));
}

// ============================================================================
// 7. Sweep parsing
// ============================================================================

TEST_CASE("SoundTheme: sweep parsing with target/end", "[sound][theme]") {
    auto theme = SoundThemeParser::load_from_string(kCompleteThemeJson);
    REQUIRE(theme.has_value());

    auto& nav = theme->sounds.at("nav_forward");
    auto& sweep = nav.steps[0].sweep;

    CHECK(sweep.target == "freq");
    CHECK(sweep.end_value == Approx(2400));
}

// ============================================================================
// 8. Filter parsing
// ============================================================================

TEST_CASE("SoundTheme: filter parsing with type/cutoff/sweep_to", "[sound][theme]") {
    auto theme = SoundThemeParser::load_from_string(kCompleteThemeJson);
    REQUIRE(theme.has_value());

    auto& nav = theme->sounds.at("nav_forward");
    auto& filter = nav.steps[0].filter;

    CHECK(filter.type == "lowpass");
    CHECK(filter.cutoff == Approx(800));
    CHECK(filter.sweep_to == Approx(4000));
}

// ============================================================================
// 9. Step with "note" field uses note_to_freq
// ============================================================================

TEST_CASE("SoundTheme: step with note field resolves to freq", "[sound][theme]") {
    auto theme = SoundThemeParser::load_from_string(kCompleteThemeJson);
    REQUIRE(theme.has_value());

    auto& pc = theme->sounds.at("print_complete");
    REQUIRE(pc.steps.size() == 4);

    // C5 = 523.25 Hz
    CHECK(pc.steps[0].freq_hz == Approx(523.25f).epsilon(0.01));
    // E5 = 659.25 Hz
    CHECK(pc.steps[1].freq_hz == Approx(659.25f).epsilon(0.01));
    // G5 = 783.99 Hz
    CHECK(pc.steps[2].freq_hz == Approx(783.99f).epsilon(0.01));
    // C6 = 1046.50 Hz
    CHECK(pc.steps[3].freq_hz == Approx(1046.50f).epsilon(0.01));
}

// ============================================================================
// 10. Step with "freq" field uses raw Hz
// ============================================================================

TEST_CASE("SoundTheme: step with freq field uses raw Hz", "[sound][theme]") {
    auto theme = SoundThemeParser::load_from_string(kCompleteThemeJson);
    REQUIRE(theme.has_value());

    auto& tap = theme->sounds.at("button_tap");
    CHECK(tap.steps[0].freq_hz == Approx(4000));
}

// ============================================================================
// 11. Step with "pause" field creates pause step
// ============================================================================

TEST_CASE("SoundTheme: pause step", "[sound][theme]") {
    auto theme = SoundThemeParser::load_from_string(kCompleteThemeJson);
    REQUIRE(theme.has_value());

    auto& alert = theme->sounds.at("error_alert");
    REQUIRE(alert.steps.size() == 3);

    // Step 1 (index 1) is a pause
    CHECK(alert.steps[1].is_pause == true);
    CHECK(alert.steps[1].duration_ms == Approx(80));
    CHECK(alert.steps[1].freq_hz == Approx(0));
}

// ============================================================================
// 12. Theme defaults applied to steps that omit wave/vel/env
// ============================================================================

TEST_CASE("SoundTheme: theme defaults applied to steps", "[sound][theme]") {
    auto theme = SoundThemeParser::load_from_string(kCompleteThemeJson);
    REQUIRE(theme.has_value());

    // toggle_on steps don't specify wave or vel — should get theme defaults
    auto& toggle = theme->sounds.at("toggle_on");
    CHECK(toggle.steps[0].wave == Waveform::TRIANGLE); // theme default
    CHECK(toggle.steps[0].velocity == Approx(0.7f));   // theme default

    // button_tap explicitly specifies wave and vel — should NOT use defaults
    auto& tap = theme->sounds.at("button_tap");
    CHECK(tap.steps[0].wave == Waveform::SQUARE); // explicitly set
    CHECK(tap.steps[0].velocity == Approx(0.9f)); // explicitly set
}

// ============================================================================
// 13. Invalid JSON returns nullopt (not crash)
// ============================================================================

TEST_CASE("SoundTheme: invalid JSON returns nullopt", "[sound][theme]") {
    CHECK_FALSE(SoundThemeParser::load_from_string("not json at all").has_value());
    CHECK_FALSE(SoundThemeParser::load_from_string("{broken").has_value());
    CHECK_FALSE(SoundThemeParser::load_from_string("").has_value());
    CHECK_FALSE(SoundThemeParser::load_from_string("null").has_value());
    CHECK_FALSE(SoundThemeParser::load_from_string("42").has_value());
}

// ============================================================================
// 14. Missing "sounds" key returns nullopt
// ============================================================================

TEST_CASE("SoundTheme: missing sounds key returns nullopt", "[sound][theme]") {
    const char* json = R"({
        "name": "no-sounds",
        "version": 1
    })";
    CHECK_FALSE(SoundThemeParser::load_from_string(json).has_value());
}

// ============================================================================
// 15. Empty sounds map is valid
// ============================================================================

TEST_CASE("SoundTheme: empty sounds map is valid", "[sound][theme]") {
    auto theme = SoundThemeParser::load_from_string(kMinimalThemeJson);
    REQUIRE(theme.has_value());

    CHECK(theme->name == "test-theme");
    CHECK(theme->sounds.empty());
}

// ============================================================================
// 16. Unknown wave type defaults to SQUARE with warning
// ============================================================================

TEST_CASE("SoundTheme: unknown wave type defaults to SQUARE", "[sound][theme]") {
    const char* json = R"({
        "name": "bad-wave",
        "version": 1,
        "sounds": {
            "test": {
                "steps": [
                    { "freq": 440, "dur": 100, "wave": "wobble" }
                ]
            }
        }
    })";
    auto theme = SoundThemeParser::load_from_string(json);
    REQUIRE(theme.has_value());

    auto& step = theme->sounds.at("test").steps[0];
    CHECK(step.wave == Waveform::SQUARE);
}

// ============================================================================
// 17. Frequency clamped to 20-20000 Hz range
// ============================================================================

TEST_CASE("SoundTheme: frequency clamped to audible range", "[sound][theme]") {
    const char* json = R"({
        "name": "clamp-test",
        "version": 1,
        "sounds": {
            "low": {
                "steps": [{ "freq": 5, "dur": 100 }]
            },
            "high": {
                "steps": [{ "freq": 50000, "dur": 100 }]
            },
            "normal": {
                "steps": [{ "freq": 440, "dur": 100 }]
            }
        }
    })";
    auto theme = SoundThemeParser::load_from_string(json);
    REQUIRE(theme.has_value());

    CHECK(theme->sounds.at("low").steps[0].freq_hz == Approx(20));
    CHECK(theme->sounds.at("high").steps[0].freq_hz == Approx(20000));
    CHECK(theme->sounds.at("normal").steps[0].freq_hz == Approx(440));
}

// ============================================================================
// 18. Duration clamped to 1-30000 ms
// ============================================================================

TEST_CASE("SoundTheme: duration clamped to valid range", "[sound][theme]") {
    const char* json = R"({
        "name": "dur-clamp",
        "version": 1,
        "sounds": {
            "short": {
                "steps": [{ "freq": 440, "dur": 0.1 }]
            },
            "long": {
                "steps": [{ "freq": 440, "dur": 99999 }]
            }
        }
    })";
    auto theme = SoundThemeParser::load_from_string(json);
    REQUIRE(theme.has_value());

    CHECK(theme->sounds.at("short").steps[0].duration_ms == Approx(1));
    CHECK(theme->sounds.at("long").steps[0].duration_ms == Approx(30000));
}

// ============================================================================
// 19. Velocity clamped to 0.0-1.0
// ============================================================================

TEST_CASE("SoundTheme: velocity clamped to 0.0-1.0", "[sound][theme]") {
    const char* json = R"({
        "name": "vel-clamp",
        "version": 1,
        "sounds": {
            "quiet": {
                "steps": [{ "freq": 440, "dur": 100, "vel": -0.5 }]
            },
            "loud": {
                "steps": [{ "freq": 440, "dur": 100, "vel": 2.0 }]
            }
        }
    })";
    auto theme = SoundThemeParser::load_from_string(json);
    REQUIRE(theme.has_value());

    CHECK(theme->sounds.at("quiet").steps[0].velocity == Approx(0.0f));
    CHECK(theme->sounds.at("loud").steps[0].velocity == Approx(1.0f));
}

// ============================================================================
// 20. BPM on sound definition overrides theme-level for duration calc
// ============================================================================

TEST_CASE("SoundTheme: BPM on sound definition used for musical durations", "[sound][theme]") {
    auto theme = SoundThemeParser::load_from_string(kCompleteThemeJson);
    REQUIRE(theme.has_value());

    auto& pc = theme->sounds.at("print_complete");
    CHECK(pc.bpm == Approx(140));

    // At 140 BPM: quarter note = 60000/140 = ~428.57ms
    // 8n = half of that = ~214.29ms
    // Steps 0-2 are "8n", step 3 is "4n"
    CHECK(pc.steps[0].duration_ms == Approx(214.29f).epsilon(0.1));
    CHECK(pc.steps[3].duration_ms == Approx(428.57f).epsilon(0.1));
}

// ============================================================================
// 21. Repeat field parsed correctly, defaults to 1
// ============================================================================

TEST_CASE("SoundTheme: repeat field", "[sound][theme]") {
    auto theme = SoundThemeParser::load_from_string(kCompleteThemeJson);
    REQUIRE(theme.has_value());

    // error_alert has repeat: 3
    CHECK(theme->sounds.at("error_alert").repeat == 3);

    // button_tap doesn't specify repeat — should default to 1
    CHECK(theme->sounds.at("button_tap").repeat == 1);
}

// ============================================================================
// Edge cases: theme with no defaults section
// ============================================================================

TEST_CASE("SoundTheme: theme without defaults section uses struct defaults", "[sound][theme]") {
    const char* json = R"({
        "name": "no-defaults",
        "version": 1,
        "sounds": {
            "beep": {
                "steps": [{ "freq": 1000, "dur": 100 }]
            }
        }
    })";
    auto theme = SoundThemeParser::load_from_string(json);
    REQUIRE(theme.has_value());

    // Without defaults section, struct defaults should be used
    CHECK(theme->default_wave == Waveform::SQUARE);
    CHECK(theme->default_velocity == Approx(0.8f));
    CHECK(theme->default_envelope.attack_ms == Approx(5));
    CHECK(theme->default_envelope.decay_ms == Approx(40));
    CHECK(theme->default_envelope.sustain_level == Approx(0.6f));
    CHECK(theme->default_envelope.release_ms == Approx(80));

    // Steps should inherit struct defaults
    auto& step = theme->sounds.at("beep").steps[0];
    CHECK(step.wave == Waveform::SQUARE);
    CHECK(step.velocity == Approx(0.8f));
}

// ============================================================================
// Edge case: load_from_file with nonexistent file
// ============================================================================

TEST_CASE("SoundTheme: load_from_file with nonexistent file returns nullopt", "[sound][theme]") {
    CHECK_FALSE(SoundThemeParser::load_from_file("/nonexistent/path/theme.json").has_value());
}
