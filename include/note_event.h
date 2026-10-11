// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "sound_synthesis.h"
#include "sound_theme.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <mutex>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/// All parameters for one note, written atomically by the sequencer.
/// The audio callback snapshots this on generation change.
struct NoteEvent {
    float freq_hz = 0;
    float sweep_end_freq = 0; // 0 = no sweep
    float velocity = 0;       // peak amplitude (pre-scaled by master volume)
    float duration_ms = 0;    // total step duration
    float duty_cycle = 0.5f;
    Waveform wave = Waveform::SQUARE;

    // ADSR
    float attack_ms = 5;
    float decay_ms = 40;
    float sustain_level = 0.6f;
    float release_ms = 80;

    // LFO (0 rate = disabled)
    float lfo_rate = 0;
    float lfo_depth = 0;
    uint8_t lfo_target = 0; // 0=none, 1=freq, 2=amplitude, 3=duty

    // Filter (0 cutoff = no filter)
    float filter_cutoff = 0;
    float filter_sweep_to = 0;
    uint8_t filter_type = 0; // 0=none, 1=lowpass, 2=highpass
};

/// Per-voice state in a PCM backend.
/// `event` + `generation` are written by the sequencer thread through
/// publish()/edit() and read by the audio thread through start_pending_note().
/// Everything else is owned by the audio callback / render thread.
struct VoiceSlot {
    // --- Written by sequencer, read by callback, both under event_mutex ---
    NoteEvent event;
    std::atomic<uint32_t> generation{0};
    std::mutex event_mutex;

    // --- Owned by audio callback thread only ---
    uint32_t cb_generation = 0;
    float phase = 0;
    float elapsed_samples = 0;
    float current_amplitude = 0;
    helix::audio::BiquadFilter filter;

    // Snapshot of event params (copied on generation change)
    NoteEvent active;

    /// Sequencer side: replace the pending note and announce it.
    void publish(const NoteEvent& e) {
        edit([&](NoteEvent& ev) { ev = e; }, true);
    }

    /// Sequencer side: change fields of the pending note. `restart` announces
    /// it as a new note; without it the change applies at the next note start.
    template <typename F> void edit(F&& fn, bool restart) {
        std::lock_guard<std::mutex> lock(event_mutex);
        fn(event);
        if (restart)
            generation.fetch_add(1, std::memory_order_release);
    }

    /// Audio side: start the pending note if one was announced. Never blocks:
    /// if the sequencer is mid-publish the note starts on the next call.
    bool start_pending_note(float sample_rate) {
        if (generation.load(std::memory_order_acquire) == cb_generation)
            return false;
        std::unique_lock<std::mutex> lock(event_mutex, std::try_to_lock);
        if (!lock.owns_lock())
            return false;
        cb_generation = generation.load(std::memory_order_relaxed);
        reset_for_new_note();
        lock.unlock();
        if (active.filter_type != 0) {
            auto ft = (active.filter_type == 1) ? helix::audio::FilterType::LOWPASS
                                                : helix::audio::FilterType::HIGHPASS;
            helix::audio::compute_biquad_coeffs(filter, ft, active.filter_cutoff, sample_rate);
        }
        return true;
    }

    /// Snapshot `event` into `active` and restart the voice. Single-threaded
    /// callers use it directly; a live audio thread goes through
    /// start_pending_note().
    void reset_for_new_note() {
        active = event;
        phase = 0;
        elapsed_samples = 0;
        current_amplitude = 0;
        // Reset filter state for new note
        if (active.filter_type != 0) {
            auto ft = (active.filter_type == 1) ? helix::audio::FilterType::LOWPASS
                                                : helix::audio::FilterType::HIGHPASS;
            helix::audio::compute_biquad_coeffs(filter, ft, active.filter_cutoff,
                                                0); // sample_rate filled by caller
            filter.z1 = 0;
            filter.z2 = 0;
        } else {
            filter.active = false;
        }
    }

    /// Compute one sample: envelope * waveform, with sweep/LFO/filter.
    /// Returns the amplitude-scaled sample value.
    float render_sample(float sample_rate) {
        if (active.velocity <= 0.001f) {
            current_amplitude = 0;
            return 0;
        }

        float elapsed_ms = elapsed_samples * 1000.0f / sample_rate;
        float total_ms =
            std::max(active.duration_ms, active.attack_ms + active.decay_ms + active.release_ms);

        // Past end of note
        if (elapsed_ms >= total_ms) {
            current_amplitude = 0;
            elapsed_samples++;
            return 0;
        }

        // Progress through the step (0..1)
        float progress = (total_ms > 0) ? std::clamp(elapsed_ms / total_ms, 0.0f, 1.0f) : 1.0f;

        // --- Frequency (with sweep + LFO) ---
        float freq = active.freq_hz;
        if (active.sweep_end_freq > 0) {
            freq = active.freq_hz + (active.sweep_end_freq - active.freq_hz) * progress;
        }
        if (active.lfo_rate > 0 && active.lfo_target == 1) {
            float lfo =
                std::sin(2.0f * static_cast<float>(M_PI) * active.lfo_rate * elapsed_ms / 1000.0f);
            freq += lfo * active.lfo_depth;
        }
        freq = std::clamp(freq, 20.0f, 20000.0f);

        // --- Duty cycle (with LFO) ---
        float duty = active.duty_cycle;
        if (active.lfo_rate > 0 && active.lfo_target == 3) {
            float lfo =
                std::sin(2.0f * static_cast<float>(M_PI) * active.lfo_rate * elapsed_ms / 1000.0f);
            duty = std::clamp(duty + lfo * active.lfo_depth, 0.0f, 1.0f);
        }

        // --- ADSR envelope ---
        float env_amp = compute_envelope(elapsed_ms, total_ms);

        // --- Amplitude LFO ---
        float amp = env_amp * active.velocity;
        if (active.lfo_rate > 0 && active.lfo_target == 2) {
            float lfo =
                std::sin(2.0f * static_cast<float>(M_PI) * active.lfo_rate * elapsed_ms / 1000.0f);
            amp = std::clamp(amp + lfo * active.lfo_depth, 0.0f, 1.0f);
        }

        current_amplitude = amp;

        // --- Generate one sample of waveform ---
        float phase_inc = freq / sample_rate;
        float sample = 0;
        switch (active.wave) {
        case Waveform::SQUARE:
            sample = (phase < duty) ? amp : -amp;
            break;
        case Waveform::SAW:
            sample = amp * (2.0f * phase - 1.0f);
            break;
        case Waveform::TRIANGLE:
            sample = amp * (4.0f * std::abs(phase - 0.5f) - 1.0f);
            break;
        case Waveform::SINE:
            sample = amp * std::sin(2.0f * static_cast<float>(M_PI) * phase);
            break;
        }

        phase += phase_inc;
        phase -= std::floor(phase);
        elapsed_samples++;
        return sample;
    }

  private:
    float compute_envelope(float elapsed_ms, float total_ms) const {
        float a = active.attack_ms;
        float d = active.decay_ms;
        float s = active.sustain_level;
        float r = active.release_ms;

        if (a <= 0 && d <= 0 && r <= 0)
            return 1.0f;

        float release_start = total_ms - r;

        if (elapsed_ms < a) {
            return (a > 0) ? (elapsed_ms / a) : 1.0f;
        } else if (elapsed_ms < a + d) {
            float decay_progress = (d > 0) ? ((elapsed_ms - a) / d) : 1.0f;
            return 1.0f - (1.0f - s) * decay_progress;
        } else if (elapsed_ms < release_start) {
            return s;
        } else {
            float release_elapsed = elapsed_ms - release_start;
            float release_progress = (r > 0) ? std::clamp(release_elapsed / r, 0.0f, 1.0f) : 1.0f;
            return s * (1.0f - release_progress);
        }
    }
};
