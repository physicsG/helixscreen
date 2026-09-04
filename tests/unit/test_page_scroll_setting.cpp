// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#include "display_settings_manager.h"

#include "../catch_amalgamated.hpp"

TEST_CASE("page_scroll_buttons setting round-trips", "[page_scroll_buttons][settings]") {
    auto& mgr = helix::DisplaySettingsManager::instance();
    mgr.init_subjects();

    mgr.set_page_scroll_buttons(true);
    CHECK(mgr.get_page_scroll_buttons());
    CHECK(lv_subject_get_int(mgr.subject_page_scroll_buttons()) == 1);

    mgr.set_page_scroll_buttons(false); // reset for other tests
    CHECK_FALSE(mgr.get_page_scroll_buttons());
    CHECK(lv_subject_get_int(mgr.subject_page_scroll_buttons()) == 0);
}
