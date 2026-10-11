// Copyright (C) 2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_probe_config_defaults.cpp
 * @brief Probe config rows for options left out of printer.cfg.
 *
 * Klipper's configfile.config lists only what printer.cfg sets, so a stock
 * [probe] has no x_offset, samples, ... at all. Those rows show the value
 * Klipper actually uses, and the edit field starts empty instead of holding
 * a placeholder word that would be written back as the value.
 */

#include "ui_probe_overlay.h"

#include "../lvgl_test_fixture.h"
#include "../test_helpers/probe_overlay_test_access.h"

#include <string>

#include "../catch_amalgamated.hpp"

using helix::ui::probe_config_display_value;
using helix::ui::probe_config_klipper_default;

TEST_CASE("unset probe options show Klipper's default value", "[probe][config_defaults]") {
    CHECK(probe_config_klipper_default("x_offset") == "0");
    CHECK(probe_config_klipper_default("y_offset") == "0");
    CHECK(probe_config_klipper_default("samples") == "1");
    CHECK(probe_config_klipper_default("speed") == "5");
    CHECK(probe_config_klipper_default("sample_retract_dist") == "2");
    CHECK(probe_config_klipper_default("samples_tolerance") == "0.1");

    CHECK(probe_config_display_value("samples", "") == "1 (Default)");
    CHECK(probe_config_display_value("x_offset", "") == "0 (Default)");
}

TEST_CASE("a configured probe option shows as written", "[probe][config_defaults]") {
    CHECK(probe_config_display_value("samples", "3") == "3");
    CHECK(probe_config_display_value("x_offset", "-40.0") == "-40.0");
}

TEST_CASE_METHOD(LVGLTestFixture, "editing an unset probe option starts with an empty field",
                 "[probe][config_defaults]") {
    ProbeOverlay overlay;
    overlay.init_subjects();

    overlay.handle_config_edit("samples", "Samples", "");
    CHECK(ProbeOverlayTestAccess::edit_value(overlay).empty());
    overlay.handle_config_cancel();

    ProbeOverlayTestAccess::stage_configured(overlay, "speed", "7.5");
    overlay.handle_config_edit("speed", "Speed", "");
    CHECK(ProbeOverlayTestAccess::edit_value(overlay) == "7.5");

    overlay.handle_config_cancel();
}
