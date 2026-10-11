// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "refresh_timing_env.h"
#include "test_helpers/scoped_env.h"

#include <cstdlib>
#include <string>
#include <vector>

#include "../../catch_amalgamated.hpp"

using helix::RefreshTiming;
using helix::ScopedEnv;

namespace {

constexpr const char* REFR_PERIOD = "HELIX_REFR_PERIOD_MS";
constexpr const char* REFR_SCOPE = "HELIX_REFR_PERIOD_SCOPE";
constexpr const char* SAVER_PERIOD = "HELIX_SCREENSAVER_REFR_PERIOD_MS";
constexpr const char* LOOP_FLOOR = "HELIX_LOOP_MIN_SLEEP_MS";
constexpr const char* EGL_VSYNC = "HELIX_EGL_VSYNC";
constexpr const char* EGL_PARTIAL = "HELIX_EGL_PARTIAL_UPLOAD";
constexpr const char* EGL_XRGB = "HELIX_EGL_XRGB";

/// Restores every refresh-timing variable when the test ends, and starts from none set.
struct RefreshEnv {
    ScopedEnv period{REFR_PERIOD};
    ScopedEnv scope{REFR_SCOPE};
    ScopedEnv saver{SAVER_PERIOD};
    ScopedEnv floor{LOOP_FLOOR};
    ScopedEnv vsync{EGL_VSYNC};
    ScopedEnv partial{EGL_PARTIAL};
    ScopedEnv xrgb{EGL_XRGB};

    RefreshEnv() {
        for (const char* name : {REFR_PERIOD, REFR_SCOPE, SAVER_PERIOD, LOOP_FLOOR, EGL_VSYNC,
                                 EGL_PARTIAL, EGL_XRGB}) {
            unsetenv(name);
        }
    }
};

} // namespace

TEST_CASE("refresh timing defaults when nothing is set", "[application][display][refresh_period]") {
    RefreshEnv env;
    const RefreshTiming t = helix::refresh_timing_from_env();
    CHECK(t.refr_period_ms == 0);
    CHECK_FALSE(t.scope_all);
    CHECK(t.screensaver_refr_period_ms == 16);
    CHECK(t.loop_min_sleep_ms == 5);
    CHECK(t.screensaver_loop_min_sleep_ms == 1);
    CHECK(helix::egl_vsync_from_env());
    CHECK(helix::egl_partial_upload_from_env());
    CHECK(helix::egl_xrgb_from_env());

    // A default-constructed value carries the same defaults.
    const RefreshTiming defaults;
    CHECK(defaults.screensaver_refr_period_ms == t.screensaver_refr_period_ms);
    CHECK(defaults.screensaver_loop_min_sleep_ms == t.screensaver_loop_min_sleep_ms);
}

TEST_CASE("refresh timing reads each variable", "[application][display][refresh_period]") {
    RefreshEnv env;
    setenv(REFR_PERIOD, "16", 1);
    setenv(REFR_SCOPE, "all", 1);
    setenv(SAVER_PERIOD, "20", 1);
    setenv(LOOP_FLOOR, "2", 1);
    setenv(EGL_VSYNC, "1", 1);

    const RefreshTiming t = helix::refresh_timing_from_env();
    CHECK(t.refr_period_ms == 16);
    CHECK(t.scope_all);
    CHECK(t.screensaver_refr_period_ms == 20);
    CHECK(t.loop_min_sleep_ms == 2);
    CHECK(t.screensaver_loop_min_sleep_ms == 2);
    CHECK(helix::egl_vsync_from_env());

    SECTION("a display scope, vsync 0 and screensaver period 0 read as off") {
        setenv(REFR_SCOPE, "display", 1);
        setenv(EGL_VSYNC, "0", 1);
        setenv(SAVER_PERIOD, "0", 1);
        const RefreshTiming off = helix::refresh_timing_from_env();
        CHECK_FALSE(off.scope_all);
        CHECK_FALSE(helix::egl_vsync_from_env());
        CHECK(off.screensaver_refr_period_ms == 0);
    }
}

TEST_CASE("refresh periods accept 8 to 100 ms and reject everything else",
          "[application][display][refresh_period]") {
    RefreshEnv env;
    const char* name = GENERATE(REFR_PERIOD, SAVER_PERIOD);
    CAPTURE(name);

    for (const char* ok : {"8", "100"}) {
        CAPTURE(ok);
        setenv(name, ok, 1);
        const RefreshTiming t = helix::refresh_timing_from_env();
        const uint32_t got =
            std::string(name) == REFR_PERIOD ? t.refr_period_ms : t.screensaver_refr_period_ms;
        CHECK(got == std::strtoul(ok, nullptr, 10));
    }

    for (const char* bad : {"", "7", "101", "-16", "16ms", "abc", " 16", "4294967312"}) {
        CAPTURE(bad);
        setenv(name, bad, 1);
        const RefreshTiming t = helix::refresh_timing_from_env();
        CHECK(t.refr_period_ms == 0);
        CHECK(t.screensaver_refr_period_ms == RefreshTiming::DEFAULT_SCREENSAVER_REFR_PERIOD_MS);
    }
}

TEST_CASE("the main loop floor accepts 1 to 33 ms and rejects everything else",
          "[application][display][refresh_period]") {
    RefreshEnv env;
    for (const char* ok : {"1", "33"}) {
        CAPTURE(ok);
        setenv(LOOP_FLOOR, ok, 1);
        const RefreshTiming t = helix::refresh_timing_from_env();
        // A set floor applies everywhere, screensaver included.
        CHECK(t.loop_min_sleep_ms == std::strtoul(ok, nullptr, 10));
        CHECK(t.screensaver_loop_min_sleep_ms == std::strtoul(ok, nullptr, 10));
    }
    for (const char* bad : {"", "0", "34", "-1", "5x", "fast"}) {
        CAPTURE(bad);
        setenv(LOOP_FLOOR, bad, 1);
        const RefreshTiming t = helix::refresh_timing_from_env();
        CHECK(t.loop_min_sleep_ms == 5);
        CHECK(t.screensaver_loop_min_sleep_ms == 1);
    }
}

TEST_CASE("unknown scope and EGL switch values keep the defaults",
          "[application][display][refresh_period]") {
    RefreshEnv env;
    for (const char* bad : {"ALL", "everything", "", "1"}) {
        CAPTURE(bad);
        setenv(REFR_SCOPE, bad, 1);
        CHECK_FALSE(helix::refresh_timing_from_env().scope_all);
    }
    for (const char* bad : {"yes", "true", "2", ""}) {
        CAPTURE(bad);
        setenv(EGL_VSYNC, bad, 1);
        CHECK(helix::egl_vsync_from_env());
    }
}

TEST_CASE("main loop sleep keeps today's caps and floor by default",
          "[application][display][refresh_period]") {
    const RefreshTiming defaults;
    CHECK(helix::main_loop_sleep_ms(100, false, defaults) == 33);
    CHECK(helix::main_loop_sleep_ms(20, false, defaults) == 20);
    CHECK(helix::main_loop_sleep_ms(0, false, defaults) == 5);
    CHECK(helix::main_loop_sleep_ms(1000, true, defaults) == 200);
    CHECK(helix::main_loop_sleep_ms(150, true, defaults) == 150);
    CHECK(helix::main_loop_sleep_ms(0, true, defaults) == 5);
}

TEST_CASE("main loop sleep follows the configured floor",
          "[application][display][refresh_period]") {
    RefreshTiming t;
    t.loop_min_sleep_ms = 1;
    CHECK(helix::main_loop_sleep_ms(0, false, t) == 1);
    CHECK(helix::main_loop_sleep_ms(3, false, t) == 3);

    t.loop_min_sleep_ms = 10;
    CHECK(helix::main_loop_sleep_ms(3, false, t) == 10);
    CHECK(helix::main_loop_sleep_ms(3, true, t) == 10);
}

TEST_CASE("the awake cap follows a refresh period longer than 33 ms",
          "[application][display][refresh_period]") {
    RefreshTiming t;
    t.refr_period_ms = 50;
    CHECK(helix::main_loop_sleep_ms(100, false, t) == 50);
    CHECK(helix::main_loop_sleep_ms(40, false, t) == 40);

    // A shorter period leaves the cap where it is: lv_timer_handler() already wakes the
    // loop in time for a faster refresh timer.
    t.refr_period_ms = 16;
    CHECK(helix::main_loop_sleep_ms(100, false, t) == 33);

    // Sleeping keeps its own cap.
    t.refr_period_ms = 100;
    CHECK(helix::main_loop_sleep_ms(1000, true, t) == 200);
}

TEST_CASE("HELIX_EGL_VSYNC reaches the vsync setter unless it is 0",
          "[application][display][refresh_period]") {
    RefreshEnv env;
    std::vector<bool> calls;
    const auto record = [&calls](bool on) { calls.push_back(on); };

    CHECK(helix::apply_egl_vsync_from_env(record));
    REQUIRE(calls.size() == 1);
    setenv(EGL_VSYNC, "0", 1);
    CHECK_FALSE(helix::apply_egl_vsync_from_env(record));
    CHECK(calls.size() == 1);
    setenv(EGL_VSYNC, "on", 1);
    CHECK(helix::apply_egl_vsync_from_env(record));
    setenv(EGL_VSYNC, "1", 1);
    CHECK(helix::apply_egl_vsync_from_env(record));
    REQUIRE(calls.size() == 3);
    CHECK(calls == std::vector<bool>{true, true, true});
}

TEST_CASE("each EGL presentation switch is on unless its own variable is 0",
          "[application][display][refresh_period][egl_upload]") {
    RefreshEnv env;
    CHECK(helix::egl_partial_upload_from_env());
    CHECK(helix::egl_xrgb_from_env());

    setenv(EGL_PARTIAL, "0", 1);
    CHECK_FALSE(helix::egl_partial_upload_from_env());
    CHECK(helix::egl_xrgb_from_env());
    CHECK(helix::egl_vsync_from_env());

    unsetenv(EGL_PARTIAL);
    setenv(EGL_XRGB, "0", 1);
    CHECK_FALSE(helix::egl_xrgb_from_env());
    CHECK(helix::egl_partial_upload_from_env());
    CHECK(helix::egl_vsync_from_env());

    // Only 0 turns a switch off: 1 and anything malformed keep it on.
    for (const char* on : {"1", "yes", "true", "on", "2", "", " 0", "0 "}) {
        CAPTURE(on);
        setenv(EGL_PARTIAL, on, 1);
        setenv(EGL_XRGB, on, 1);
        CHECK(helix::egl_partial_upload_from_env());
        CHECK(helix::egl_xrgb_from_env());
    }
}

TEST_CASE("an EGL switch reaches the driver unless it is 0, and reports a refusal",
          "[application][display][refresh_period][egl_upload]") {
    RefreshEnv env;
    int calls = 0;
    const auto accept = [&calls] {
        ++calls;
        return true;
    };
    const auto refuse = [&calls] {
        ++calls;
        return false;
    };

    CHECK(helix::apply_egl_switch_from_env(EGL_PARTIAL, accept) == helix::EglSwitch::On);
    CHECK(calls == 1);
    setenv(EGL_PARTIAL, "0", 1);
    CHECK(helix::apply_egl_switch_from_env(EGL_PARTIAL, accept) == helix::EglSwitch::Off);
    CHECK(calls == 1);
    setenv(EGL_PARTIAL, "on", 1);
    CHECK(helix::apply_egl_switch_from_env(EGL_PARTIAL, accept) == helix::EglSwitch::On);
    CHECK(calls == 2);

    setenv(EGL_PARTIAL, "1", 1);
    CHECK(helix::apply_egl_switch_from_env(EGL_PARTIAL, refuse) == helix::EglSwitch::Declined);
    CHECK(calls == 3);
}
