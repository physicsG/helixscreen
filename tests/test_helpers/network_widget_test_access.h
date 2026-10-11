// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "src/ui/panel_widgets/network_widget.h"

namespace helix {

// Friend access to NetworkWidget's cached tile pointers and its signal-poll
// timer. The teardown contract is which of those survive which deletion path,
// and that is only observable on the privates. Read-only.
//
// Defined in ONE place, in namespace helix to match the friend declaration.
// Follows the tests/test_helpers/ TestAccess pattern ([L088]).
class NetworkWidgetTestAccess {
  public:
    static lv_obj_t* widget_obj(const NetworkWidget& widget) {
        return widget.widget_obj_;
    }

    static lv_obj_t* parent_screen(const NetworkWidget& widget) {
        return widget.parent_screen_;
    }

    static lv_timer_t* signal_poll_timer(const NetworkWidget& widget) {
        return widget.signal_poll_timer_;
    }
};

} // namespace helix
