// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "alsa_device_enum.h"

#include "../catch_amalgamated.hpp"

using namespace helix::audio;

TEST_CASE("make_pcm_name builds a plughw string", "[sound][alsa-device]") {
    CHECK(make_pcm_name("vc4hdmi1") == "plughw:CARD=vc4hdmi1,DEV=0");
}

TEST_CASE("make_label uses name with id, falls back to id", "[sound][alsa-device]") {
    CHECK(make_label("vc4hdmi1", "vc4-hdmi-1") == "vc4-hdmi-1 (vc4hdmi1)");
    CHECK(make_label("vc4hdmi1", "") == "vc4hdmi1");
}

TEST_CASE("assemble always puts System default first", "[sound][alsa-device]") {
    auto list = assemble({});
    REQUIRE(list.size() == 1);
    CHECK(list[0].pcm == "default");
    CHECK(list[0].id.empty());
    CHECK(list[0].label == "System default");
}

TEST_CASE("assemble adds one entry per playback card, skips capture-only", "[sound][alsa-device]") {
    std::vector<RawCard> cards = {
        {1, "vc4hdmi0", "vc4-hdmi-0", true},
        {2, "vc4hdmi1", "vc4-hdmi-1", true},
        {3, "capturecard", "Some Mic", false},
    };
    auto list = assemble(cards);
    REQUIRE(list.size() == 3); // System default + 2 playback cards
    CHECK(list[0].pcm == "default");
    CHECK(list[1].pcm == "plughw:CARD=vc4hdmi0,DEV=0");
    CHECK(list[2].pcm == "plughw:CARD=vc4hdmi1,DEV=0");
    CHECK(list[2].label == "vc4-hdmi-1 (vc4hdmi1)");
}

TEST_CASE("assemble dedups identical pcm strings", "[sound][alsa-device]") {
    std::vector<RawCard> cards = {
        {1, "vc4hdmi0", "a", true},
        {1, "vc4hdmi0", "a", true},
    };
    auto list = assemble(cards);
    REQUIRE(list.size() == 2); // System default + one
}

TEST_CASE("resolve_alsa_device precedence: env > settings > default", "[sound][alsa-device]") {
    CHECK(resolve_alsa_device("plughw:CARD=vc4hdmi1,DEV=0", "hw:1") == "hw:1");
    CHECK(resolve_alsa_device("plughw:CARD=vc4hdmi1,DEV=0", nullptr) ==
          "plughw:CARD=vc4hdmi1,DEV=0");
    CHECK(resolve_alsa_device("plughw:CARD=vc4hdmi1,DEV=0", "") == "plughw:CARD=vc4hdmi1,DEV=0");
    CHECK(resolve_alsa_device("", nullptr) == "default");
    CHECK(resolve_alsa_device("", "") == "default");
}

TEST_CASE("only hardware PCM names are opened", "[sound][alsa-device]") {
    CHECK(is_safe_pcm_name("default"));
    CHECK(is_safe_pcm_name("sysdefault"));
    CHECK(is_safe_pcm_name("sysdefault:CARD=Headphones"));
    CHECK(is_safe_pcm_name("hw:0,0"));
    CHECK(is_safe_pcm_name("dmix:CARD=1"));
    CHECK(is_safe_pcm_name(make_pcm_name("vc4hdmi1")));
    CHECK_FALSE(is_safe_pcm_name("file:FILE=|touch /tmp/pwned"));
    CHECK_FALSE(is_safe_pcm_name("plughw:0|x"));
    CHECK_FALSE(is_safe_pcm_name("tee:hw:0,'/tmp/x'"));
    CHECK_FALSE(is_safe_pcm_name("plug:{type file}"));
    CHECK_FALSE(is_safe_pcm_name(""));
}

TEST_CASE("an unsafe PCM name from env or settings falls through to the next source",
          "[sound][alsa-device]") {
    CHECK(resolve_alsa_device("hw:1", "file:FILE=|cmd") == "hw:1");
    CHECK(resolve_alsa_device("file:FILE=|cmd", nullptr) == "default");
    CHECK(resolve_alsa_device("tee:x", "file:x") == "default");
}

TEST_CASE("list() always offers System default first", "[sound][alsa-device]") {
    // Non-flaky invariant: regardless of host hardware (or none), the picker
    // list is never empty and always leads with the System default entry.
    auto devices = list();
    REQUIRE(!devices.empty());
    CHECK(devices[0].pcm == "default");
    CHECK(devices[0].id.empty());
}

#include "sound_manager.h"

TEST_CASE("set_output_device is a no-op without an ALSA backend", "[sound][alsa-device]") {
    // In the test build (no HELIX_HAS_ALSA, no backend), this must safely return false.
    CHECK_FALSE(helix::SoundManager::instance().set_output_device("plughw:CARD=x,DEV=0"));
    CHECK_FALSE(helix::SoundManager::instance().has_alsa_backend());
}
