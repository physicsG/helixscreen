// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Running a macro from a one-tap home-screen control: the dangerous-macro confirmation,
// the parameter prompt, the Settings > Safety run confirmation and the saved parameter
// defaults, decided by decide_macro_run(). The Favorite Macro widget and the Toolhead
// widget's macro slot both run macros through here.

#include <lvgl.h>
#include <string>

namespace helix {

class IMoonrakerAPI;

struct HomeMacroRun {
    std::string macro_name;
    /// The control's own "Require Confirmation?" choice; a dangerous macro is confirmed
    /// whatever it says.
    bool require_confirmation = true;
    /// Where a confirmation or parameter dialog hangs; nullptr runs without dialogs except
    /// that a dangerous macro is then refused.
    lv_obj_t* parent_screen = nullptr;
    const char* caller_tag = "HomeMacro"; ///< log tag
};

/// Runs or prompts as decide_macro_run() says. Never touches the calling widget afterwards:
/// every dialog callback captures copies, so the widget may be gone when it fires.
void run_home_macro(IMoonrakerAPI* api, const HomeMacroRun& run);

} // namespace helix
