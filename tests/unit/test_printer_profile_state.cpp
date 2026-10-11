// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../lvgl_test_fixture.h"
#include "printer_profile_state.h"

#include "../catch_amalgamated.hpp"

using namespace helix;

TEST_CASE("PrinterProfileState resolves a type once and honours external persistence",
          "[printer_state][profile][1401]") {
    LVGLTestFixture fixture;
    PrinterProfileState profile;
    profile.init_subjects(false);

    // No database entry: the strategy follows the probe.
    REQUIRE(profile.set_printer_type("Unknown Printer", true, false, false));
    CHECK(profile.printer_type() == "Unknown Printer");
    CHECK(profile.z_offset_calibration_strategy() == ZOffsetCalibrationStrategy::PROBE_CALIBRATE);
    CHECK(std::string(lv_subject_get_string(profile.get_printer_type_subject())) ==
          "Unknown Printer");
    CHECK(lv_subject_get_int(profile.get_z_offset_can_save_subject()) == 1);

    // Same type, same strategy: nothing to do.
    CHECK_FALSE(profile.set_printer_type("Unknown Printer", true, false, false));

    // A module that persists the offset forces FIRMWARE_MANAGED on re-resolution.
    REQUIRE(profile.set_external_persistence(true));
    CHECK_FALSE(profile.set_external_persistence(true));
    REQUIRE(profile.set_printer_type("Unknown Printer", true, false, false));
    CHECK(profile.z_offset_calibration_strategy() == ZOffsetCalibrationStrategy::FIRMWARE_MANAGED);
    CHECK(lv_subject_get_int(profile.get_z_offset_can_save_subject()) == 0);

    // Refuted: the type-derived strategy comes back.
    REQUIRE(profile.set_external_persistence(false));
    REQUIRE(profile.set_printer_type("Unknown Printer", false, false, false));
    CHECK(profile.z_offset_calibration_strategy() == ZOffsetCalibrationStrategy::ENDSTOP);
    CHECK(lv_subject_get_int(profile.get_z_offset_can_save_subject()) == 1);

    profile.deinit_subjects();
}

TEST_CASE("PrinterProfileState merges firmware option defaults as deltas",
          "[printer_state][profile]") {
    PrinterProfileState profile;
    CHECK(profile.merge_firmware_option_defaults({{"bed_mesh", true}}));
    CHECK_FALSE(profile.merge_firmware_option_defaults({{"bed_mesh", true}}));
    CHECK(profile.merge_firmware_option_defaults({{"priming", false}}));
    CHECK(profile.merge_firmware_option_defaults({{"bed_mesh", false}}));
}

TEST_CASE("PrinterProfileState synthesizes the timelapse option only while available",
          "[printer_state][profile]") {
    PrinterProfileState profile;
    profile.set_timelapse_default_enabled(true);

    profile.apply_dynamic_options(false, true);
    const auto* tl = profile.pre_print_option_set().find("timelapse");
    REQUIRE(tl != nullptr);
    CHECK(tl->default_enabled);

    profile.apply_dynamic_options(false, true);
    CHECK(profile.pre_print_option_set().options.size() == 1);

    profile.apply_dynamic_options(false, false);
    CHECK(profile.pre_print_option_set().find("timelapse") == nullptr);
}
