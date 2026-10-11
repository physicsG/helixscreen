// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "src/ui/panel_widgets/gcode_console_widget.h"

namespace helix {

// Friend access to GCodeConsoleWidget's tail state. Follows the
// tests/test_helpers/ TestAccess pattern ([L088]).
class GCodeConsoleWidgetTestAccess {
  public:
    static const std::deque<ConsolePanel::GcodeEntry>& lines(const GCodeConsoleWidget& w) {
        return w.lines_;
    }
    static const std::string& handler_name(const GCodeConsoleWidget& w) {
        return w.handler_name_;
    }
};

} // namespace helix
