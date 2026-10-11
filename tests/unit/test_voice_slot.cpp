// SPDX-License-Identifier: GPL-3.0-or-later
// VoiceSlot is the handoff between the sequencer thread and a PCM backend's
// audio thread; these tests drive both sides concurrently.

#include "note_event.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>

#include "../catch_amalgamated.hpp"

namespace {

// Every field carries k, so a snapshot mixing two publishes is detectable.
NoteEvent note_with_value(float k) {
    NoteEvent e;
    e.freq_hz = k;
    e.sweep_end_freq = k;
    e.velocity = k;
    e.duration_ms = k;
    e.duty_cycle = k;
    e.attack_ms = k;
    e.decay_ms = k;
    e.sustain_level = k;
    e.release_ms = k;
    e.lfo_rate = k;
    e.lfo_depth = k;
    e.filter_cutoff = k;
    e.filter_sweep_to = k;
    return e;
}

bool is_consistent(const NoteEvent& e) {
    float k = e.freq_hz;
    return e.sweep_end_freq == k && e.velocity == k && e.duration_ms == k && e.duty_cycle == k &&
           e.attack_ms == k && e.decay_ms == k && e.sustain_level == k && e.release_ms == k &&
           e.lfo_rate == k && e.lfo_depth == k && e.filter_cutoff == k && e.filter_sweep_to == k;
}

} // namespace

TEST_CASE("VoiceSlot: a published note starts on the next pending check", "[sound][voice_slot]") {
    VoiceSlot slot;
    REQUIRE_FALSE(slot.start_pending_note(44100.0f));

    NoteEvent e = note_with_value(440.0f);
    e.filter_type = 1;
    slot.publish(e);
    REQUIRE(slot.start_pending_note(44100.0f));
    CHECK(slot.active.freq_hz == 440.0f);
    CHECK(slot.filter.active);
    CHECK_FALSE(slot.start_pending_note(44100.0f));
}

TEST_CASE("VoiceSlot: audio thread never snapshots a half-written note", "[sound][voice_slot]") {
    VoiceSlot slot;
    std::atomic<bool> done{false};
    std::atomic<int> torn{0};
    std::atomic<int> started{0};

    std::thread audio([&] {
        while (!done.load(std::memory_order_acquire)) {
            if (slot.start_pending_note(44100.0f)) {
                started.fetch_add(1, std::memory_order_relaxed);
                if (!is_consistent(slot.active))
                    torn.fetch_add(1, std::memory_order_relaxed);
            }
        }
    });

    // Keep publishing until the audio thread has taken many notes mid-stream,
    // however late a loaded scheduler first runs it. The deadline only bounds
    // a slot that never hands a note over.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    for (int64_t i = 1; i <= 200000 || (started.load(std::memory_order_relaxed) < 100 &&
                                        std::chrono::steady_clock::now() < deadline);
         ++i)
        slot.publish(note_with_value(static_cast<float>(i % 1000000 + 1)));
    done.store(true, std::memory_order_release);
    audio.join();

    CHECK(started.load() > 0);
    CHECK(torn.load() == 0);
}
