// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#ifdef HELIX_HAS_TRACKER

#ifndef HELIX_HAS_SOUND
#error "HELIX_HAS_TRACKER requires HELIX_HAS_SOUND"
#endif

#include "sound_theme.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace helix::audio {

/// A single note event in a tracker pattern cell
struct TrackerNote {
    uint8_t note = 0;       // 0=none, 1-84 = C-0..B-6
    uint8_t instrument = 0; // 0=none
    uint8_t effect = 0;
    uint8_t effect_data = 0;
    uint16_t period = 0; // raw Amiga period (for accurate sample playback)
};

/// Instrument definition derived from tracker sample headers
struct TrackerInstrument {
    Waveform waveform = Waveform::SQUARE; // fallback when no sample data
    float volume = 1.0f;                  // 0.0-1.0, from sample volume field
    float finetune = 0.0f;                // semitone offset, from sample finetune field

    // PCM sample data (8-bit signed, converted to float on load)
    std::vector<float> sample_data; // normalized to -1.0..1.0
    uint32_t loop_start = 0;        // in samples
    uint32_t loop_length = 0;       // 0 = no loop
    uint16_t c4_rate = 8287;        // sample rate at which sample plays C-4 (PAL Amiga default)

    bool has_sample() const {
        return !sample_data.empty();
    }
};

/// A parsed tracker module (MOD or MED format)
///
/// patterns[pat][row * 4 + ch] — 4 channels, 64 rows per pattern by default.
struct TrackerModule {
    std::vector<TrackerInstrument> instruments;
    std::vector<std::vector<TrackerNote>> patterns; // [pattern][row*4+channel]
    std::vector<uint8_t> order;
    uint8_t num_orders = 0;
    uint8_t speed = 6;
    uint8_t tempo = 125;
    uint16_t rows_per_pattern = 64;
    bool has_samples = false; // true if any instrument has PCM sample data

    /// Load a module from a file on disk
    static std::optional<TrackerModule> load(const std::string& path);

    /// Load a module from a memory buffer (detects MOD vs MED by magic)
    static std::optional<TrackerModule> load_from_memory(const uint8_t* data, size_t size);

    /// Convert a note number to frequency in Hz.
    /// note 0 → 0 Hz (silence), note 58 → 440 Hz (A-4)
    static float note_to_freq(uint8_t note);

    /// Convert a note number to Amiga period value.
    /// Uses the standard ProTracker period table. note 0 → 0 (silence).
    /// Notes 1-84 map to periods from 1712 (C-0) down to 14 (B-6).
    static uint16_t note_to_period(uint8_t note);
};

/// Parse a ProTracker MOD file from memory
std::optional<TrackerModule> parse_mod(const uint8_t* data, size_t size);

} // namespace helix::audio

#endif // HELIX_HAS_TRACKER
