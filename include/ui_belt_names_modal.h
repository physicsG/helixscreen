// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ui_modal.h"

namespace helix::ui {

/**
 * @brief Belt Tension's "Which belt is which?" dialog
 *
 * Each path's Identify button buzzes that path's motor (the panel sends it and
 * gates it); the layout buttons write settings_belt_layout, which the panel
 * observes to rename every path label. Done only closes: a layout is saved
 * the moment it is tapped.
 */
class BeltNamesModal : public Modal {
  public:
    const char* get_name() const override {
        return "Belt Names";
    }
    const char* component_name() const override {
        return "belt_names_modal";
    }

    /// Register the layout buttons' callback. Idempotent.
    static void register_callbacks();

  protected:
    void on_show() override;
};

} // namespace helix::ui
