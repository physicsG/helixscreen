// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#include "app_globals.h"
#include "async_lifetime_guard.h"
#include "lvgl_test_fixture.h"
#include "moonraker_error.h"
#include "printer_state.h"
#include "toolhead_homing.h"

#include "../catch_amalgamated.hpp"

TEST_CASE("toolhead_is_homed reads the live homed_axes subject", "[homing][toolhead]") {
    LVGLTestFixture fixture;
    helix::PrinterState ps;
    ps.init_subjects(true);

    SECTION("empty means not homed") {
        lv_subject_copy_string(ps.get_homed_axes_subject(), "");
        CHECK_FALSE(helix::toolhead_is_homed(ps));
    }
    SECTION("xyz means homed") {
        lv_subject_copy_string(ps.get_homed_axes_subject(), "xyz");
        CHECK(helix::toolhead_is_homed(ps));
    }
    SECTION("partial homing is NOT homed") {
        lv_subject_copy_string(ps.get_homed_axes_subject(), "xy");
        CHECK_FALSE(helix::toolhead_is_homed(ps));
    }
    SECTION("z-only is NOT homed") {
        lv_subject_copy_string(ps.get_homed_axes_subject(), "z");
        CHECK_FALSE(helix::toolhead_is_homed(ps));
    }
}

TEST_CASE("axis_is_homed reads each axis independently", "[homing][toolhead]") {
    LVGLTestFixture fixture;
    helix::PrinterState ps;
    ps.init_subjects(false); // false = skip XML registration (test default)

    SECTION("xy homed leaves z unhomed") {
        lv_subject_copy_string(ps.get_homed_axes_subject(), "xy");
        CHECK(helix::axis_is_homed(ps, helix::Axis::X));
        CHECK(helix::axis_is_homed(ps, helix::Axis::Y));
        CHECK_FALSE(helix::axis_is_homed(ps, helix::Axis::Z));
        CHECK_FALSE(helix::toolhead_is_homed(ps));
    }
    SECTION("empty homed_axes means nothing is homed") {
        lv_subject_copy_string(ps.get_homed_axes_subject(), "");
        CHECK_FALSE(helix::axis_is_homed(ps, helix::Axis::X));
        CHECK_FALSE(helix::axis_is_homed(ps, helix::Axis::Y));
        CHECK_FALSE(helix::axis_is_homed(ps, helix::Axis::Z));
    }
    SECTION("xyz homed reports all three axes") {
        lv_subject_copy_string(ps.get_homed_axes_subject(), "xyz");
        CHECK(helix::axis_is_homed(ps, helix::Axis::X));
        CHECK(helix::axis_is_homed(ps, helix::Axis::Y));
        CHECK(helix::axis_is_homed(ps, helix::Axis::Z));
    }
}

TEST_CASE("free ensure_homed_then runs then() synchronously when homed", "[homing][toolhead]") {
    LVGLTestFixture fixture;
    // ensure_homed_then() has no PrinterState parameter — it reads the
    // process-wide singleton via get_printer_state(), same as every caller
    // it replaces. Drive that instance directly rather than a disconnected
    // local PrinterState (which owns its own, unrelated subject storage).
    helix::PrinterState& state = get_printer_state();
    state.init_subjects(false);
    lv_subject_copy_string(state.get_homed_axes_subject(), "xyz");

    helix::AsyncLifetimeGuard guard;
    bool ran = false;
    bool error_ran = false;
    helix::ensure_homed_then(
        nullptr, guard, [&ran] { ran = true; },
        [&error_ran](const MoonrakerError&) { error_ran = true; });
    CHECK(ran);
    CHECK_FALSE(error_ran);
}

TEST_CASE("free ensure_homed_then does not run then() synchronously when not homed",
          "[homing][toolhead]") {
    LVGLTestFixture fixture;
    helix::PrinterState& state = get_printer_state();
    state.init_subjects(false);
    lv_subject_copy_string(state.get_homed_axes_subject(), "");

    helix::AsyncLifetimeGuard guard;
    bool ran = false;
    // api == nullptr: there is nothing to send G28 with. Must not crash on
    // a null dereference, and then() must not fire synchronously — only a
    // real G28 completion (which cannot happen without an API) may run it.
    helix::ensure_homed_then(nullptr, guard, [&ran] { ran = true; }, nullptr);
    CHECK_FALSE(ran);
}
