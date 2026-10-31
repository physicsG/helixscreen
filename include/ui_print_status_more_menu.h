// Copyright (C) 2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui_context_menu.h"

namespace helix::ui {

/**
 * @brief Print status's "More" menu: camera, timelapse and light
 *
 * Each row keeps the visibility rule its strip button had, bound in XML, so a
 * printer without a webcam lists no Camera row. The action id is one of the
 * kAction* constants, or kActionCancelled.
 */
class PrintStatusMoreMenu : public ContextMenu {
    HELIX_CONTEXT_MENU_KIND(PrintStatusMoreMenu)

  public:
    static constexpr int kActionCamera = 0;
    static constexpr int kActionTimelapse = 1;
    static constexpr int kActionLight = 2;

    /// Register the menu's row callbacks. Idempotent.
    static void register_callbacks();

  protected:
    const char* xml_component_name() const override {
        return "print_status_more_menu";
    }

  private:
    static void pick(int action);
};

} // namespace helix::ui
