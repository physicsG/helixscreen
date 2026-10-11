// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "src/ui/panel_widgets/tiled_panel_widget.h"

#include <string>
#include <utility>

namespace helix {

/**
 * @brief A centred-icon tile whose only C++ behaviour is sizing itself
 *
 * Several home tiles are pure XML: the component draws everything and no C++
 * instance exists. Such a tile cannot size itself, because measuring needs
 * on_size_changed() and binding needs per-instance subject names to exist
 * before the component is parsed, and both of those live on an instance.
 *
 * Rather than six near-identical widget classes, one class covers every tile
 * whose only need is a TileSizing. A tile that grows real behaviour later stops
 * using this and implements PanelWidget directly.
 */
class TileWidget : public TiledPanelWidget {
  public:
    TileWidget(std::string instance_id, TileSizing::Content content)
        : TiledPanelWidget(instance_id, std::move(content)), instance_id_(std::move(instance_id)) {}

    /// The component carries the whole appearance and the sizing subjects are
    /// live from construction, so there is nothing to wire.
    void attach(lv_obj_t*, lv_obj_t*) override {}
    void detach() override {}

    const char* id() const override {
        return instance_id_.c_str();
    }

    /// The instance id carries a suffix for multi-instance tiles
    /// ("power_device:1"), but the component name is per TYPE, so the suffix is
    /// dropped here or lv_xml_create is handed a name no component answers to.
    std::string get_component_name() const override {
        const size_t colon = instance_id_.find(':');
        return "panel_widget_" +
               (colon == std::string::npos ? instance_id_ : instance_id_.substr(0, colon));
    }

  private:
    std::string instance_id_;
};

} // namespace helix
