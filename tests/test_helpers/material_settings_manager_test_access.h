// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "material_settings_manager.h"

namespace helix {

// Friend of MaterialSettingsManager (declared as `TestAccess`): returns the
// singleton to its never-initialized defaults.
class TestAccess {
  public:
    static void reset(MaterialSettingsManager& mgr) {
        mgr.overrides_.clear();
        mgr.preset_materials_ = default_preset_materials();
        mgr.preset_filaments_ = {};
        mgr.initialized_ = false;
    }
};

} // namespace helix
