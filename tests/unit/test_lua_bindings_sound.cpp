// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#if HELIX_HAS_PLUGINS

#include "../lvgl_test_fixture.h"
#include "../test_helpers/log_capture.h"
#include "../test_helpers/plugin_test_support.h"
#include "lua_bindings.h"

#include <string>
#include <utility>
#include <vector>

#include "../catch_amalgamated.hpp"

using namespace helix::plugin;
using namespace helix::plugin::test;

namespace {

/// (name, priority) of every cue the fake backend was asked to play.
std::vector<std::pair<std::string, std::string>> sounds(const BoundRuntime& b) {
    std::vector<std::pair<std::string, std::string>> out;
    for (const auto& r : b.fake.requests) {
        if (r.kind == "sound")
            out.emplace_back(r.a, r.b);
    }
    return out;
}

} // namespace

TEST_CASE_METHOD(LVGLTestFixture, "helix.sound.play hands the cue and priority to the app",
                 "[plugin][lua_bindings_sound]") {
    BoundRuntime b({&install_sound_bindings});
    REQUIRE(b.t.run(R"(ok1 = helix.sound.play("button_tap"))"));
    process_lvgl(2000);
    REQUIRE(b.t.run(R"(ok2 = helix.sound.play("print_complete", "event"))"));
    process_lvgl(2000);
    REQUIRE(b.t.run(R"(ok3 = helix.sound.play("error_alert", "ui"))"));
    CHECK(b.t.global("ok1") == "true");
    CHECK(b.t.global("ok2") == "true");
    CHECK(b.t.global("ok3") == "true");
    using P = std::pair<std::string, std::string>;
    CHECK(sounds(b) == std::vector<P>{P{"button_tap", "ui"}, P{"print_complete", "event"},
                                      P{"error_alert", "ui"}});
}

TEST_CASE_METHOD(LVGLTestFixture, "helix.sound.play accepts only the documented cues",
                 "[plugin][lua_bindings_sound]") {
    for (const char* name :
         {"button_tap", "toggle_on", "toggle_off", "nav_forward", "nav_back", "dropdown_open",
          "print_complete", "print_cancelled", "error_alert", "error_tone", "test_beep"}) {
        INFO(name);
        BoundRuntime b({&install_sound_bindings});
        REQUIRE(b.t.run(std::string("helix.sound.play('") + name + "')"));
        REQUIRE(sounds(b).size() == 1);
        CHECK(sounds(b)[0].first == name);
    }
    // The boot chime and the alarm are the app's own: a plugin cannot sound either.
    for (const char* lua : {R"(helix.sound.play("startup"))", R"(helix.sound.play("alarm_urgent"))",
                            R"(helix.sound.play("no_such_cue"))", R"(helix.sound.play(""))",
                            R"(helix.sound.play())", R"(helix.sound.play({}))"}) {
        INFO(lua);
        BoundRuntime b({&install_sound_bindings});
        CHECK_FALSE(b.t.run(lua));
        CHECK(sounds(b).empty());
        CHECK_FALSE(b.t.rt->faulted());
    }
}

TEST_CASE_METHOD(LVGLTestFixture, "helix.sound.play refuses the alarm priority",
                 "[plugin][lua_bindings_sound]") {
    for (const char* lua :
         {R"(helix.sound.play("error_alert", "alarm"))",
          R"(helix.sound.play("error_alert", "loud"))", R"(helix.sound.play("error_alert", 2))"}) {
        INFO(lua);
        BoundRuntime b({&install_sound_bindings});
        helix::LogCapture logs;
        CHECK_FALSE(b.t.run(lua));
        CHECK(sounds(b).empty());
    }
}

TEST_CASE_METHOD(LVGLTestFixture, "helix.sound.play drops cues that come too fast",
                 "[plugin][lua_bindings_sound]") {
    BoundRuntime b({&install_sound_bindings});
    REQUIRE(b.t.run(R"(a = helix.sound.play("button_tap"); b = helix.sound.play("button_tap"))"));
    CHECK(b.t.global("a") == "true");
    CHECK(b.t.global("b") == "false");
    CHECK(sounds(b).size() == 1);

    process_lvgl(kPluginUiCueIntervalMs - 10);
    REQUIRE(b.t.run(R"(c = helix.sound.play("toggle_on"))"));
    CHECK(b.t.global("c") == "false");
    process_lvgl(20);
    REQUIRE(b.t.run(R"(d = helix.sound.play("toggle_on"))"));
    CHECK(b.t.global("d") == "true");
    CHECK(sounds(b).size() == 2);

    // Event cues hold the speaker against UI sounds, so they have a longer interval of
    // their own; a UI cue in between neither waits for it nor resets it.
    REQUIRE(b.t.run(R"(e = helix.sound.play("print_complete", "event"))"));
    CHECK(b.t.global("e") == "true");
    process_lvgl(kPluginUiCueIntervalMs + 10);
    REQUIRE(b.t.run(R"(f = helix.sound.play("button_tap"))"));
    CHECK(b.t.global("f") == "true");
    process_lvgl(kPluginEventCueIntervalMs - kPluginUiCueIntervalMs - 40);
    REQUIRE(b.t.run(R"(g = helix.sound.play("error_alert", "event"))"));
    CHECK(b.t.global("g") == "false");
    process_lvgl(40);
    REQUIRE(b.t.run(R"(h = helix.sound.play("error_alert", "event"))"));
    CHECK(b.t.global("h") == "true");
    CHECK(sounds(b).size() == 5);
}

TEST_CASE_METHOD(LVGLTestFixture, "each plugin has its own cue interval",
                 "[plugin][lua_bindings_sound]") {
    BoundRuntime a({&install_sound_bindings});
    BoundRuntime b({&install_sound_bindings});
    REQUIRE(a.t.run(R"(ok = helix.sound.play("button_tap"))"));
    REQUIRE(b.t.run(R"(ok = helix.sound.play("button_tap"))"));
    CHECK(a.t.global("ok") == "true");
    CHECK(b.t.global("ok") == "true");
}

TEST_CASE_METHOD(LVGLTestFixture, "helix.sound.play without an app sound path is a no-op",
                 "[plugin][lua_bindings_sound]") {
    BoundRuntime b({&install_sound_bindings});
    b.backend.play_sound = nullptr;
    REQUIRE(b.t.run(R"(ok = helix.sound.play("button_tap"))"));
    CHECK(b.t.global("ok") == "false");
    CHECK(b.fake.requests.empty());
}

#endif // HELIX_HAS_PLUGINS
