// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <stdbool.h>

// I2C bus + GT911 + LVGL pointer indev. Call on the UI pthread after lv_init.
// Returns false if the controller could not be brought up (typically a
// degraded touch ribbon failing the GT911 probe); in that case no indev is
// registered and the board runs display-only rather than resetting.
bool touch_input_init(void);

// Whether touch_input_init() registered a working pointer indev. The main/-side
// query for any future consumer; the boot warning path gets the value pushed
// down via app_boot_set_touch_available() instead (main depends on helixapp,
// never the reverse). No extern "C" guard, like its main/ siblings — include
// from C only.
bool touch_input_available(void);

/// Press at (x, y) and release, queued like a tap the controller reported.
/// Safe from any task, like the two below.
void touch_input_inject_tap(int x, int y);

/// A new finger lands at (x, y); a finger still down lifts first, as when fast
/// typing rolls over from one key to the next.
void touch_input_inject_down(int x, int y);

/// Every injected finger lifts.
void touch_input_inject_up(void);

/// Taps dropped whole because LVGL fell a full queue behind, since boot.
unsigned touch_input_dropped_taps(void);
