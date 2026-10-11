// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <vector>

namespace helix::audio {

/// A selectable audio output. `id`/`pcm` empty+"default" for the System default entry.
struct AudioOutputDevice {
    std::string id;    // ALSA card id, e.g. "vc4hdmi1" ("" for System default)
    std::string label; // user-facing, e.g. "vc4-hdmi-1 (vc4hdmi1)" / "System default"
    std::string pcm;   // snd_pcm_open arg: "plughw:CARD=vc4hdmi1,DEV=0" / "default"
};

/// Raw card facts gathered from ALSA (or synthesized in tests).
struct RawCard {
    int index = 0;
    std::string id;   // ALSA card id
    std::string name; // human card name
    bool has_playback = false;
};

// --- Pure helpers (always compiled; unit-tested) ---

/// "plughw:CARD=<id>,DEV=0"
std::string make_pcm_name(const std::string& card_id);

/// Human label: "<name> (<id>)", or bare id when name is empty.
std::string make_label(const std::string& card_id, const std::string& card_name);

/// Build the picker list: always [0] = "System default" (pcm "default"),
/// then one entry per playback-capable card, deduped by pcm.
std::vector<AudioOutputDevice> assemble(const std::vector<RawCard>& cards);

/// True for the PCM names HelixScreen opens: `default`, `sysdefault[:...]`,
/// `hw:...`, `plughw:...` or `dmix:...`, with no `|`, `file` or `tee` anywhere.
/// ALSA's file plugin popen()s a `|cmd` target, so any other name is code.
bool is_safe_pcm_name(const std::string& pcm);

/// Precedence resolver. env wins, then settings, then "default"; a value
/// is_safe_pcm_name() rejects is skipped. env may be nullptr/"".
std::string resolve_alsa_device(const std::string& settings_device, const char* env_device);

// --- Live wrappers (read real ALSA / settings / env) ---

/// Enumerate selectable outputs. Without ALSA, returns just System default.
std::vector<AudioOutputDevice> list();

/// resolve_alsa_device() using HELIX_ALSA_DEVICE and the persisted setting.
std::string resolve_alsa_device();

} // namespace helix::audio
