// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ui_wizard_ams_identify.h"

#include <string>

// Test-only reach into the AMS identify step's private type naming. Follows the
// tests/test_helpers/ TestAccess pattern ([L088]).
namespace helix::ui {

struct WizardAmsIdentifyStepTestAccess {
    static std::string type_name(const WizardAmsIdentifyStep& step) {
        return step.get_ams_type_name();
    }
};

} // namespace helix::ui
