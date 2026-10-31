// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The app surfaces that hold back a plugin surface. A plugin dialog, overlay or
// confirm never covers one of these; it waits until none is showing.

namespace helix::ui {

struct AppCovers {
    bool app_modal = false;      ///< a visible ModalStack entry with no tag
    bool external_cover = false; ///< a modal surface outside the stack (the PIN pad)
    bool lock_screen = false;    ///< the lock screen
    bool aborting = false;       ///< the abort progress dialog
    bool busy = false;           ///< the busy overlay
};

/// True when any app surface is up.
bool app_blocked(const AppCovers& c);

/// Reads every app cover from its owner. Main thread only.
AppCovers current_app_covers();

} // namespace helix::ui
