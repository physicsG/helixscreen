// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui_context_menu.h"

namespace helix::ui {

/**
 * @brief Belt Tension's "Re-test" menu: which path to measure again
 *
 * On the smallest screens RESULTS has room for three buttons, not four, so
 * the two Re-test buttons fold into one button that raises this menu. The
 * action id is the RESULTS header slot of the row's path (0 = left, see
 * helix::ui::belt_path_in_slot), or kActionCancelled.
 */
class BeltRetestMenu : public ContextMenu {
    HELIX_CONTEXT_MENU_KIND(BeltRetestMenu)

  public:
    static constexpr int kActionSlot0 = 0;
    static constexpr int kActionSlot1 = 1;

    /// Register the menu's row callbacks. Idempotent.
    static void register_callbacks();

  protected:
    const char* xml_component_name() const override {
        return "belt_retest_menu";
    }

  private:
    static void pick(int action);
};

} // namespace helix::ui
