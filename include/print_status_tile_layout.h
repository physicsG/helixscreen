// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// How the print status panel arranges its job tile, printer tile and strip in
// the content box it was given. Pure: measured pixels in, arrangement out.

#include <cstdint>

namespace helix {

enum class PrintStatusArrangement { SideBySide = 0, Stacked = 1 };

struct PrintStatusTileLayout {
    PrintStatusArrangement arrangement = PrintStatusArrangement::SideBySide;
    bool strip_stacked = false;     ///< progress above the buttons instead of beside them
    bool checklist_compact = false; ///< current step + dots instead of the list
    int32_t job_w = 0, job_h = 0;   ///< the job tile's size, for the chip and font decisions
    int32_t printer_w = 0, printer_h = 0;
    int32_t strip_h = 0;
};

/// `content_w` x `content_h` is the panel's content box, `gap` the token gap,
/// `strip_row_h` / `strip_stacked_h` the strip heights the current tier's fonts need.
[[nodiscard]] PrintStatusTileLayout decide_print_status_tile_layout(int32_t content_w,
                                                                    int32_t content_h, int32_t gap,
                                                                    int32_t strip_row_h,
                                                                    int32_t strip_stacked_h);

} // namespace helix
