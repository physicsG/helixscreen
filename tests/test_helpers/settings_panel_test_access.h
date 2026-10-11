// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/**
 * @file settings_panel_test_access.h
 * @brief The friend accessor for reaching SettingsPanel's private
 *        EthernetManager from tests.
 *
 * ethernet_manager_ is lazily created on the first refresh_status_lines()
 * call and picks EthernetBackendMock automatically under test_mode, so a test
 * driving the mock's connected state has to go through the panel rather than
 * construct its own manager.
 */

#include "ui_panel_settings.h"

#include "ethernet_manager.h"

// SettingsPanel is a global-scope class (no namespace), so this accessor is
// too -- a namespace-qualified friend wouldn't match.
class SettingsPanelTestAccess {
  public:
    static EthernetManager* ethernet_manager(SettingsPanel& panel) {
        return panel.ethernet_manager_.get();
    }
};
