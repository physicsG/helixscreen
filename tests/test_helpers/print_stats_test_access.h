// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "src/ui/panel_widgets/print_stats_widget.h"

namespace helix {

// Friend access to PrintStatsWidget's cached tile pointers. The teardown
// contract is which raw pointers survive which deletion path, and that is only
// observable on the privates: the widget publishes through static subjects, so
// nothing in the visible UI state distinguishes a dropped pointer from a
// dangling one. Read-only.
//
// Defined in ONE place, in namespace helix to match the friend declaration —
// two translation units defining their own copy would be an ODR violation.
// Follows the tests/test_helpers/ TestAccess pattern ([L088]).
class PrintStatsTestAccess {
  public:
    static lv_obj_t* widget_obj(const PrintStatsWidget& widget) {
        return widget.widget_obj_;
    }

    static lv_obj_t* parent_screen(const PrintStatsWidget& widget) {
        return widget.parent_screen_;
    }
};

} // namespace helix
