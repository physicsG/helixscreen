// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "src/ui/panel_widgets/filament_sensor_widget.h"

namespace helix {

// Reaches FilamentSensorWidget's private show_tap_modal() and its owned
// RunoutGuidanceModal. Production only enters show_tap_modal() through a tap on
// a tile that the home grid hides unless the printer has a filament sensor and
// no AMS backend — reproducible under the mock only with --no-ams and a fresh
// config dir, i.e. not from a unit test. Declared a friend of
// FilamentSensorWidget; follows the tests/test_helpers/ TestAccess pattern
// ([L088]) rather than adding _for_testing() accessors.
class FilamentSensorWidgetTestAccess {
  public:
    static void show_tap_modal(FilamentSensorWidget& widget, bool status_only) {
        widget.show_tap_modal(status_only);
    }
    static RunoutGuidanceModal& tap_modal(FilamentSensorWidget& widget) {
        return widget.tap_modal_;
    }
};

} // namespace helix
