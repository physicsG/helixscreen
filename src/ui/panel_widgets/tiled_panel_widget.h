// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "panel_widget.h"
#include "src/ui/panel_widgets/tile_sizing.h"

#include <utility>

namespace helix {

/**
 * @brief A PanelWidget whose home tile is sized by a TileSizing
 *
 * Owns the sizing and forwards the four PanelWidget hooks that reach it, so a
 * tile states its content once, in the base initializer, and writes none of the
 * forwarding. The sizing is built with the widget because its subject names
 * must exist before the manager parses the tile's component.
 *
 * A widget with its own fit rule (a wide layout, a compact one) overrides
 * fits_at() / on_size_changed() and calls the sizing directly.
 */
class TiledPanelWidget : public PanelWidget {
  public:
    void on_size_changed(int /*colspan*/, int /*rowspan*/, int width_px, int height_px) override {
        sizing_.measure_and_publish(width_px, height_px);
    }
    bool fits_at(int width_px, int height_px) const override {
        return sizing_.fits(width_px, height_px);
    }
    const char** xml_attrs() const override {
        return sizing_.subject_attrs();
    }
    TileSizing* tile_sizing() override {
        return &sizing_;
    }

  protected:
    /// Arguments are TileSizing's: (instance_id) or (instance_id, Content).
    template <typename... Args>
    explicit TiledPanelWidget(Args&&... args) : sizing_(std::forward<Args>(args)...) {}

    TileSizing sizing_;
};

} // namespace helix
