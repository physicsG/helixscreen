// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_nav_manager.h"
#include "ui_wizard.h"

#include "../helix_test_fixture.h"
#include "../lvgl_ui_test_fixture.h"
#include "app_globals.h"
#include "config.h"
#include "first_run_tour.h"
#include "tour_steps.h"

#include "../catch_amalgamated.hpp"

using helix::Config;
using helix::tour::build_tour_steps;
using helix::tour::FirstRunTour;
using helix::tour::TourStep;

namespace {
void reset_tour_settings() {
    auto* cfg = Config::get_instance();
    cfg->set<bool>("/tour/completed", false);
    cfg->set<int>("/tour/last_seen_version", 0);
    // is_wizard_required() checks per-printer first, then falls back to root-level.
    // In the singleton-without-init test environment, active_printer_id_ is empty,
    // so the root-level key is the one that's read.
    cfg->set<bool>("/wizard_completed", true);
    // Edit mode gates the tour, and the subject is global, so a case that leaves
    // it set would decide every case that runs after it.
    lv_subject_set_int(&get_home_edit_mode_subject(), 0);
    // In-memory singleton state is left clean by the public API used in these
    // tests: start()/skip() always pair, and advance() past the last step calls
    // finish() which resets running_. No explicit reset needed.
}
} // namespace

TEST_CASE("FirstRunTour gate: blocks when tour already completed", "[tour]") {
    reset_tour_settings();
    Config::get_instance()->set<bool>("/tour/completed", true);
    Config::get_instance()->set<int>("/tour/last_seen_version", helix::tour::TOUR_VERSION);
    REQUIRE(FirstRunTour::should_auto_start() == false);
}

TEST_CASE("FirstRunTour gate: blocks when wizard not complete", "[tour]") {
    reset_tour_settings();
    Config::get_instance()->set<bool>("/wizard_completed", false);
    REQUIRE(FirstRunTour::should_auto_start() == false);
}

TEST_CASE("FirstRunTour gate: allows auto-start when fresh and wizards done", "[tour]") {
    reset_tour_settings();
    REQUIRE(FirstRunTour::should_auto_start() == true);
}

TEST_CASE("FirstRunTour gate: re-triggers when last_seen_version is behind TOUR_VERSION",
          "[tour]") {
    reset_tour_settings();
    Config::get_instance()->set<bool>("/tour/completed", true);
    Config::get_instance()->set<int>("/tour/last_seen_version", 0);
    // TOUR_VERSION is 1; last_seen=0 < 1, so tour should re-trigger.
    REQUIRE(FirstRunTour::should_auto_start() == true);
}

TEST_CASE("FirstRunTour gate: blocks while home grid edit mode is active", "[tour]") {
    reset_tour_settings();
    // Pin that the gate is otherwise open, so a gate stuck at false cannot pass.
    REQUIRE(FirstRunTour::should_auto_start() == true);

    lv_subject_set_int(&get_home_edit_mode_subject(), 1);
    REQUIRE(FirstRunTour::should_auto_start() == false);

    lv_subject_set_int(&get_home_edit_mode_subject(), 0);
    REQUIRE(FirstRunTour::should_auto_start() == true);
}

TEST_CASE_METHOD(LVGLUITestFixture, "FirstRunTour: wizard completion re-evaluates the gate",
                 "[tour][wizard]") {
    reset_tour_settings();
    Config::get_instance()->set<bool>("/wizard_completed", false);

    // Home is already the active panel while the wizard owns the screen, so the
    // deferred set_active(PanelId::Home) returns without activating anything.
    // on_activate() is the tour's other entry point and does not run here.
    REQUIRE(NavigationManager::instance().get_active() == helix::PanelId::Home);
    REQUIRE(FirstRunTour::should_auto_start() == false);

    auto& tour = FirstRunTour::instance();
    REQUIRE(tour.is_running() == false);

    ui_wizard_complete();
    process_lvgl(200); // past the 100ms nav timer, and drains the async start

    REQUIRE(tour.is_running() == true);
    tour.skip();
}

TEST_CASE("FirstRunTour mark_completed writes both flags", "[tour]") {
    reset_tour_settings();
    FirstRunTour::mark_completed();
    auto* cfg = Config::get_instance();
    REQUIRE(cfg->get<bool>("/tour/completed", false) == true);
    REQUIRE(cfg->get<int>("/tour/last_seen_version", 0) == helix::tour::TOUR_VERSION);
}

TEST_CASE("Tour steps: always 8 steps regardless of AMS", "[tour]") {
    auto steps_no_ams = build_tour_steps(/*has_ams=*/false);
    auto steps_with_ams = build_tour_steps(/*has_ams=*/true);
    REQUIRE(steps_no_ams.size() == 8);
    REQUIRE(steps_with_ams.size() == 8);
}

TEST_CASE("Tour steps: step 2 targets AMS widget when present, nozzle otherwise", "[tour]") {
    auto with_ams = build_tour_steps(true);
    auto without = build_tour_steps(false);
    REQUIRE(with_ams[1].target_name == "ams");
    REQUIRE(without[1].target_name == "nozzle_temps");
}

TEST_CASE("Tour steps: step 4 is print_status widget, steps 5-8 are nav buttons", "[tour]") {
    auto steps = build_tour_steps(true);
    REQUIRE(steps[3].target_name == "print_status");
    REQUIRE(steps[4].target_name == "nav_btn_controls");
    REQUIRE(steps[5].target_name == "nav_btn_filament");
    REQUIRE(steps[6].target_name == "nav_btn_advanced");
    REQUIRE(steps[7].target_name == "nav_btn_settings");
}

TEST_CASE("Tour steps: welcome and customize steps have correct targets", "[tour]") {
    auto steps = build_tour_steps(true);
    REQUIRE(steps[0].target_name.empty());        // Welcome is centered
    REQUIRE(steps[2].target_name == "fan_stack"); // Customize example tile
}

TEST_CASE_METHOD(HelixTestFixture, "FirstRunTour: start() sets is_running", "[tour]") {
    reset_tour_settings();
    auto& t = FirstRunTour::instance();
    REQUIRE(!t.is_running());
    t.start();
    REQUIRE(t.is_running());
    t.skip();
    REQUIRE(!t.is_running());
}

TEST_CASE_METHOD(HelixTestFixture, "FirstRunTour: skip writes tour.completed", "[tour]") {
    reset_tour_settings();
    auto& t = FirstRunTour::instance();
    t.start();
    t.skip();
    REQUIRE(Config::get_instance()->get<bool>("/tour/completed", false) == true);
}

TEST_CASE_METHOD(HelixTestFixture, "FirstRunTour: advance past last step finishes", "[tour]") {
    reset_tour_settings();
    auto& t = FirstRunTour::instance();
    t.start();
    for (int i = 0; i < 8; ++i)
        t.advance();
    REQUIRE(!t.is_running());
    REQUIRE(Config::get_instance()->get<bool>("/tour/completed", false) == true);
}
