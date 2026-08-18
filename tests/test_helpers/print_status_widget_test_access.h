// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "src/ui/panel_widgets/print_status_widget.h"

namespace helix {

// Friend access to PrintStatusWidget internals that production code reaches
// only through a live dashboard. The header forward-declares this class inside
// namespace helix, so the definition must live in the same namespace — and in
// ONE place: two test translation units each defining their own version of the
// class would be an ODR violation.
//
//  - idle_thumb_path_subject(): a private static subject.
//  - dispatch_load(): the idle runout dialog's "Load filament" action, which
//    otherwise needs a live modal, an attached widget, and a real runout sensor
//    reading to press.
//  - reset_to_idle(): the idle thumbnail resolve. Production reaches it from
//    attach(), a print-state change and a history change, all of which route
//    through lv_async_call or UpdateQueue — and pumping either of those also
//    drains the queue, which destroys the ordering a staleness test has to
//    control (park one load's result, supersede it, then let it land).
//
// Follows the tests/test_helpers/ TestAccess pattern ([L088]) rather than
// adding _for_testing() accessors to the production API.
class PrintStatusWidgetTestAccess {
  public:
    static lv_subject_t* idle_thumb_path_subject() {
        return &PrintStatusWidget::idle_thumb_path_subject_;
    }

    static void dispatch_load(PrintStatusWidget& widget) {
        widget.dispatch_load();
    }

    static void reset_to_idle(PrintStatusWidget& widget) {
        widget.reset_print_card_to_idle();
    }

    // The idle thumbnail key and the freshness stamp it is validated against.
    // Production resolves both inside reset_print_card_to_idle(), where the
    // answer is only observable after a cache probe or a pool round-trip; a
    // test that only wants to know WHICH history job was selected reads them
    // straight from the two resolvers.
    static std::string last_print_thumbnail_path(const PrintStatusWidget& widget) {
        return widget.get_last_print_thumbnail_path();
    }

    static time_t last_print_source_modified(const PrintStatusWidget& widget) {
        return widget.get_last_print_source_modified();
    }
};

} // namespace helix
