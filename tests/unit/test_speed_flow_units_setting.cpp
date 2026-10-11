// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#include "display_settings_manager.h"

#include "../catch_amalgamated.hpp"

TEST_CASE("speed_flow_physical_units setting round-trips", "[tune_units][settings]") {
    auto& mgr = helix::DisplaySettingsManager::instance();
    mgr.init_subjects();

    mgr.set_speed_flow_physical_units(true);
    CHECK(mgr.get_speed_flow_physical_units());
    CHECK(lv_subject_get_int(mgr.subject_speed_flow_physical_units()) == 1);

    mgr.set_speed_flow_physical_units(false); // reset for other tests
    CHECK_FALSE(mgr.get_speed_flow_physical_units());
    CHECK(lv_subject_get_int(mgr.subject_speed_flow_physical_units()) == 0);
}
