// Copyright (C) 2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "theme_manager.h"

#include <lvgl.h>

namespace helix::test {

/// Drives the fixture display to a geometry and refreshes the derived layout
/// state (ui_breakpoint / ui_is_portrait / token tiers), restoring both on the
/// way out: the same discipline as test_is_portrait_subject.cpp's
/// RestoreDisplayConsts: theme_manager writes into a SHARED XML scope, so a
/// stale tier would decide layout for every later test in this binary.
class ScopedGeometry {
  public:
    ScopedGeometry(int32_t w, int32_t h)
        : disp_(lv_display_get_default()), w0_(lv_display_get_horizontal_resolution(disp_)),
          h0_(lv_display_get_vertical_resolution(disp_)) {
        lv_display_set_resolution(disp_, w, h);
        theme_manager_refresh_layout_constants(disp_);
    }

    ~ScopedGeometry() {
        lv_display_set_resolution(disp_, w0_, h0_);
        theme_manager_refresh_layout_constants(disp_);
    }

    ScopedGeometry(const ScopedGeometry&) = delete;
    ScopedGeometry& operator=(const ScopedGeometry&) = delete;

  private:
    lv_display_t* disp_;
    int32_t w0_;
    int32_t h0_;
};

} // namespace helix::test
