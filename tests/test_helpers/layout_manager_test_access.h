// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "layout_manager.h"

// Friend of LayoutManager: returns the singleton to its uninitialized default
// so a prior init()/set_override() cannot leak into the next test.
class LayoutManagerTestAccess {
  public:
    static void reset(helix::LayoutManager& lm) {
        lm.type_ = helix::LayoutType::STANDARD;
        lm.name_ = "standard";
        lm.override_name_.clear();
        lm.initialized_ = false;
        lm.width_ = 0;
        lm.height_ = 0;
    }
};
