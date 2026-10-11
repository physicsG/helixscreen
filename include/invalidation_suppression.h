// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <lvgl.h>

namespace helix {

/**
 * @brief One owner's disable of a display's invalidation, matched by exactly one enable
 *
 * lv_display_enable_invalidation() adjusts a count per display, and invalidation is on
 * while that count is above zero. An enable with no disable of its own raises the count
 * past 1, after which any other owner's single disable leaves invalidation on.
 *
 * - begin() disables invalidation on @p disp, or on the default display when it is null,
 *   and remembers that display. It does nothing while already active, or when there is
 *   no display.
 * - end() re-enables the display begin() disabled, once, and forgets it. It does nothing
 *   while inactive.
 *
 * The display must outlive an active suppression. Main thread only.
 */
class InvalidationSuppression {
  public:
    void begin(lv_display_t* disp = nullptr) {
        if (m_display) {
            return;
        }
        if (!disp) {
            disp = lv_display_get_default();
        }
        if (!disp) {
            return;
        }
        lv_display_enable_invalidation(disp, false);
        m_display = disp;
    }

    void end() {
        if (!m_display) {
            return;
        }
        lv_display_enable_invalidation(m_display, true);
        m_display = nullptr;
    }

    /// True between a begin() that disabled a display and the end() that re-enables it.
    bool active() const {
        return m_display != nullptr;
    }

  private:
    lv_display_t* m_display = nullptr;
};

} // namespace helix
