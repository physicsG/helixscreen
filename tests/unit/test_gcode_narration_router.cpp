// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_update_queue.h"

#include "../lvgl_test_fixture.h"
#include "../test_helpers/gcode_narration_router_test_access.h"
#include "ams_backend_afc.h"
#include "ams_state.h"
#include "gcode_narration_router.h"

#include "../catch_amalgamated.hpp"

using namespace helix;

namespace {

// AFC backend constructed with (nullptr, nullptr) — same idiom as
// AfcToolchangeTestHelper in test_afc_toolchange.cpp. AFC overrides
// toolchange_phase_template() + match_narration_phase(), which is exactly the
// step-model the router needs to resolve a `//` line to an index.
void install_afc_backend() {
    AmsState::instance().set_backend(std::make_unique<AmsBackendAfc>(nullptr, nullptr));
}

// AmsState is a process-wide singleton shared across test cases; init_subjects
// is a no-op once initialized, so the toolchange_step value persists between
// cases. Establish a known LOAD_SWAP + sentinel(-1) baseline so each test is
// order-independent.
void reset_step_baseline() {
    AmsState::instance().init_subjects(true);
    AmsState::instance().set_active_step_operation(StepOperationType::LOAD_SWAP);
    AmsState::instance().set_narration_phase(-1, "");
    install_afc_backend();
}

} // namespace

TEST_CASE_METHOD(LVGLTestFixture, "Narration router ignores non-// lines",
                 "[unit][narration][router]") {
    reset_step_baseline();

    GcodeNarrationRouter router(nullptr, nullptr); // null client => no real subscription
    GcodeNarrationRouterTestAccess::feed(router, "!! Toolhead jam");
    GcodeNarrationRouterTestAccess::feed(router, "ok");
    GcodeNarrationRouterTestAccess::feed(router, "Klipper ready");
    GcodeNarrationRouterTestAccess::feed(router, "Error: something bad");
    helix::ui::UpdateQueue::instance().drain();

    // No `//` line was fed, so the step subject stays at its sentinel.
    REQUIRE(lv_subject_get_int(AmsState::instance().get_toolchange_step_subject()) == -1);
}

TEST_CASE_METHOD(LVGLTestFixture, "Narration router advances toolchange_step on a // phase line",
                 "[unit][narration][router]") {
    reset_step_baseline();

    GcodeNarrationRouter router(nullptr, nullptr);
    // "Clean Nozzle" is AFC_BRUSH's verbose=1 wording, so the matcher returns
    // "brush"; "brush" is index 5 in the LOAD_SWAP template
    // (heat,cut,unload,feed,poop,brush,kick,load — the wipe runs before the
    // kick as well as after it, so it is ordered by first occurrence).
    GcodeNarrationRouterTestAccess::feed(router, "// AFC_Brush: Clean Nozzle");
    helix::ui::UpdateQueue::instance().drain();

    REQUIRE(lv_subject_get_int(AmsState::instance().get_toolchange_step_subject()) == 5);
}

TEST_CASE_METHOD(LVGLTestFixture, "Narration router ignores unrecognized // lines",
                 "[unit][narration][router]") {
    reset_step_baseline();

    GcodeNarrationRouter router(nullptr, nullptr);
    GcodeNarrationRouterTestAccess::feed(router, "// some unrelated chatter");
    helix::ui::UpdateQueue::instance().drain();

    // Unrecognized narration must not move the step subject off its sentinel.
    REQUIRE(lv_subject_get_int(AmsState::instance().get_toolchange_step_subject()) == -1);
}
