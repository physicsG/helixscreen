// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// How the display-message home widget draws Klipper's display_status.message
// (M117 / SET_DISPLAY_TEXT) in a box. Pure: the widget measures, this decides.

namespace helix {

/// Measured pixel inputs, all for the content box (the tile less its chrome).
struct DisplayMessageInput {
    int content_w = 0;
    int content_h = 0;
    /// One line of the message's font.
    int line_h = 0;
    /// The leading icon's glyph box.
    int icon_w = 0;
    int icon_h = 0;
    /// Flex gap between the icon and the text column.
    int gap = 0;
    /// The narrowest text column worth drawing beside the icon; the icon gives way
    /// before the text does.
    int min_text_w = 0;
};

struct DisplayMessageVerdict {
    /// Two or more lines fit: the message wraps and ends in an ellipsis on the last
    /// line that fits. Otherwise it is one line that scrolls when it is too long.
    bool multiline = false;
    bool show_icon = false;
    /// One line of text fits. What fits_at() reports.
    bool fits = false;
};

inline DisplayMessageVerdict decide_display_message_layout(const DisplayMessageInput& in) {
    DisplayMessageVerdict v;
    v.fits = in.line_h > 0 && in.content_h >= in.line_h && in.content_w > 0;
    v.multiline = in.line_h > 0 && in.content_h >= 2 * in.line_h;
    v.show_icon = in.content_h >= in.icon_h && in.content_w >= in.icon_w + in.gap + in.min_text_w;
    return v;
}

} // namespace helix
