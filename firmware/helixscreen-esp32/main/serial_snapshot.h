// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Serial console commands. "snap": the frame the panel shows comes back as raw-deflated
// RGB565, base64 on numbered, crc32-checked "SNAP:" lines between HELIX-SNAP
// markers. "snapline N": line N of the last snap again. "tap X Y": a touch
// at panel coordinates. "notes": every notification since boot as "NOTE:" lines.
// scripts/esp32_serial_snapshot.py drives snap and tap and writes a PNG.

#ifdef __cplusplus
extern "C" {
#endif

/// Start the console reader. Call once, after the console is up.
void serial_snapshot_start(void);

/// Take and stream a requested snapshot. Call from the LVGL thread only.
void serial_snapshot_poll(void);

#ifdef __cplusplus
}
#endif
