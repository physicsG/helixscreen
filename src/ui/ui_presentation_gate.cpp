// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_presentation_gate.h"

#include "ui_busy_overlay.h"
#include "ui_lock_screen.h"
#include "ui_modal.h"

#include "abort_manager.h"

namespace helix::ui {

bool app_blocked(const AppCovers& c) {
    return c.app_modal || c.external_cover || c.lock_screen || c.aborting || c.busy;
}

AppCovers current_app_covers() {
    const auto& stack = ModalStack::instance();
    const auto abort_state = AbortManager::instance().get_state();
    AppCovers c;
    c.app_modal = stack.has_visible_untagged();
    c.external_cover = stack.has_external_cover();
    c.lock_screen = LockScreenOverlay::instance().is_visible();
    c.aborting =
        abort_state != AbortManager::State::IDLE && abort_state != AbortManager::State::COMPLETE;
    c.busy = BusyOverlay::is_visible();
    return c;
}

} // namespace helix::ui
