// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "panel_widget_registry.h"

#include "../catch_amalgamated.hpp"

namespace helix {

/// Temporarily reshape a registry definition into a widget bounded to whole
/// cells: one cell minimum, two cells maximum, no half-cell steps on either
/// axis. For a test whose arithmetic is written against that shape (an even
/// snap step, an observable clamp, a span that cannot seat in a remainder)
/// and that must not move when the real widget's limits do. The registry hands
/// out const pointers because callers must not edit definitions; the whole
/// definition is restored in the destructor.
class ScopedWholeCellDef {
  public:
    explicit ScopedWholeCellDef(const char* id) {
        def_ = const_cast<PanelWidgetDef*>(find_widget_def(id));
        REQUIRE(def_ != nullptr);
        saved_ = *def_;
        def_->min_colspan = def_->min_rowspan = 2;
        def_->max_colspan = def_->max_rowspan = 4;
        def_->supports_half_col = def_->supports_half_row = false;
    }
    ~ScopedWholeCellDef() {
        *def_ = saved_;
    }
    ScopedWholeCellDef(const ScopedWholeCellDef&) = delete;
    ScopedWholeCellDef& operator=(const ScopedWholeCellDef&) = delete;

  private:
    PanelWidgetDef* def_ = nullptr;
    PanelWidgetDef saved_{};
};

} // namespace helix
