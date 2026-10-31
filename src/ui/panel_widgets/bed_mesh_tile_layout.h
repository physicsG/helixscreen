// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Pure layout decision for the bed mesh home tile.
//
// Free of LVGL so it is unit-testable without a display. BedMeshTileWidget measures the
// minimum box each rung draws in for the state being decided (mesh loaded or not, a print
// running or not) and feeds it here; this header picks the rung and the flags the XML
// binds, and composes the numbers the labels show.

#include "src/ui/panel_widgets/size_rung.h"

#include <cstdio>
#include <string>

namespace helix {

struct BedMeshTileInput {
    int content_w = 0; ///< the box the content draws in, chrome already subtracted
    int content_h = 0;
    bool mesh_loaded = false;
    bool print_active = false;
    /// The board can afford a static 3D render. Limited tiers draw the 2D heatmap at every
    /// rung.
    bool full_tier = false;
    /// The build links the heatmap renderer; without it the tile is text and glyphs only.
    bool canvas_available = true;
    /// The build links the Bed Mesh panel. Without it Load profile has nowhere to go.
    bool panel_available = true;
    int profile_count = 0;
    /// Minimum box per rung for the state above.
    RungNeeds needs;
};

/// The published verdict. `rung` is SizeRung's int; the flags are 0/1 subjects.
struct BedMeshTileVerdict {
    SizeRung rung = SizeRung::Glance;
    bool fits = false;
    int decimals = 2;             ///< 2 at Glance, 3 on every larger rung
    bool show_map = false;        ///< the heatmap canvas
    bool show_glyph = false;      ///< a grid icon stands in for the map
    bool show_range = false;      ///< the Z range on its own line, tinted by flatness (below Wide)
    bool show_empty_text = false; ///< "No mesh loaded"
    bool show_details = false;    ///< Wide: profile name, min..max, grid size
    bool show_scale = false;      ///< Full and up: the colour scale under the map
    bool show_footer = false;     ///< Full and up: profile name and profile count
    bool show_calibrate = false;  ///< Full and up, no mesh, idle
    bool show_load_profile = false; ///< as Calibrate, a stored profile exists, the panel is built
    bool render_3d = false;         ///< Large on a full-tier board: static 3D instead of 2D
};

[[nodiscard]] constexpr BedMeshTileVerdict decide_bed_mesh_tile_layout(const BedMeshTileInput& in) {
    BedMeshTileVerdict v;
    v.rung = choose_size_rung(in.content_w, in.content_h, in.needs);
    v.fits = holds_any_rung(in.content_w, in.content_h, in.needs);
    const int rung = static_cast<int>(v.rung);
    const bool at_least_strip = v.rung != SizeRung::Glance;
    const bool at_least_full = rung >= static_cast<int>(SizeRung::Full);

    v.decimals = v.rung == SizeRung::Glance ? 2 : 3;
    v.show_map = in.mesh_loaded && in.canvas_available && at_least_strip;
    v.show_glyph = !v.show_map;
    v.show_range = in.mesh_loaded && rung < static_cast<int>(SizeRung::Wide);
    v.show_empty_text = !in.mesh_loaded && rung >= static_cast<int>(SizeRung::Cell);
    v.show_details = in.mesh_loaded && v.rung == SizeRung::Wide;
    v.show_scale = in.mesh_loaded && in.canvas_available && at_least_full;
    v.show_footer = in.mesh_loaded && at_least_full;
    const bool offers_actions = !in.mesh_loaded && !in.print_active && at_least_full;
    v.show_calibrate = offers_actions;
    v.show_load_profile = offers_actions && in.profile_count > 0 && in.panel_available;
    v.render_3d = v.show_map && v.rung == SizeRung::Large && in.full_tier;
    return v;
}

/// The Z range in millimetres at @p decimals places.
[[nodiscard]] inline std::string format_mesh_range(float range_mm, int decimals) {
    char buf[24];
    std::snprintf(buf, sizeof(buf), "%.*f", decimals, static_cast<double>(range_mm));
    return buf;
}

/// "min..max" at two decimals, the form the Wide rung and the colour scale draw.
[[nodiscard]] inline std::string format_mesh_extent(float min_mm, float max_mm) {
    char buf[40];
    std::snprintf(buf, sizeof(buf), "%.2f..%.2f", static_cast<double>(min_mm),
                  static_cast<double>(max_mm));
    return buf;
}

} // namespace helix
