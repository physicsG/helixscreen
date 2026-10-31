// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ui_plr_wizard_overlay.h"

/**
 * @brief Friend access to PlrWizardOverlay's step and its button handlers.
 *
 * The handlers are what the XML callbacks call; invoking them directly drives
 * the same paths a tap does without hit-testing the rendered buttons.
 */
namespace helix::ui {

class PlrWizardOverlayTestAccess {
  public:
    static helix::PlrWizardStep step(const PlrWizardOverlay& o) {
        return o.step_;
    }
    static void tap_continue(PlrWizardOverlay& o) {
        o.handle_continue();
    }
    static void tap_resume(PlrWizardOverlay& o) {
        o.handle_resume();
    }
    static void tap_abort(PlrWizardOverlay& o) {
        o.handle_abort();
    }
    static void tap_jog(PlrWizardOverlay& o, int direction) {
        o.handle_jog(direction);
    }
};

} // namespace helix::ui
