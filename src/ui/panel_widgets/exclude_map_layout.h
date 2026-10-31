// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Pure decisions for the Exclude Objects home widget.
//
// Free of LVGL so they are unit-testable without a display. ExcludeMapWidget measures
// the minimum box each rung draws in for the state being decided and feeds it here; this
// header picks the rung and the flags the XML binds, classifies what the tile has to
// show, and counts the objects.

#include "src/ui/panel_widgets/size_rung.h"

#include <algorithm>
#include <string>
#include <unordered_set>
#include <vector>

namespace helix {

/// What the tile has to say, before any size is considered.
enum class ExcludeMapState {
    Objects,      ///< Printing, two or more objects: the map and the counts
    SingleObject, ///< Printing one object: there is nothing to skip
    NoObjects,    ///< Not printing, or the print defines none
};

/// Two objects is the least worth a map: it is also what the print status objects
/// button needs to appear.
inline constexpr int kExcludeMapMinObjects = 2;

[[nodiscard]] constexpr ExcludeMapState classify_exclude_map(bool printing, int defined_count) {
    if (!printing || defined_count <= 0)
        return ExcludeMapState::NoObjects;
    if (defined_count < kExcludeMapMinObjects)
        return ExcludeMapState::SingleObject;
    return ExcludeMapState::Objects;
}

struct ExcludeCounts {
    int total = 0;
    int skipped = 0;
    [[nodiscard]] constexpr int remaining() const {
        return total - skipped;
    }
};

/// Remaining is defined minus excluded. A name the print no longer defines (the excluded
/// set outlives a file change for a moment) is not counted as skipped.
[[nodiscard]] inline ExcludeCounts count_objects(const std::vector<std::string>& defined,
                                                 const std::unordered_set<std::string>& excluded) {
    ExcludeCounts counts;
    counts.total = static_cast<int>(defined.size());
    for (const auto& name : defined) {
        if (excluded.count(name) > 0)
            ++counts.skipped;
    }
    return counts;
}

struct ExcludeMapLayoutInput {
    int content_w = 0; ///< the box the content draws in, chrome already subtracted
    int content_h = 0;
    ExcludeMapState state = ExcludeMapState::Objects;
    /// Minimum box per rung for THIS state. A placeholder offers Glance (the icon) and
    /// Strip (the icon over its caption); the others are never chosen.
    RungNeeds needs;
    /// Full is a size class as well as a content fit: a box a 2x1 tile fills holds the
    /// map and the counts, but the rung is for 2x2 and up, so it needs at least this
    /// height.
    int full_min_h = 0;
};

/// The published verdict. `rung` is SizeRung's int; the flags are 0/1 subjects.
struct ExcludeMapVerdict {
    SizeRung rung = SizeRung::Glance;
    bool fits = false;
    bool placeholder = false;
    /// The map draws: Column and up, with objects to draw.
    bool show_map = false;
    /// Full: the "Skip objects..." button that opens the full map.
    bool show_skip_button = false;
    /// Wide and Full name the object printing now.
    bool show_name = false;
};

[[nodiscard]] constexpr ExcludeMapVerdict
decide_exclude_map_layout(const ExcludeMapLayoutInput& in) {
    ExcludeMapVerdict v;
    v.placeholder = in.state != ExcludeMapState::Objects;
    RungNeeds needs = in.needs;
    if (!v.placeholder)
        needs.full.h = std::max(needs.full.h, in.full_min_h);
    v.rung = choose_size_rung(in.content_w, in.content_h, needs);
    v.fits = holds_any_rung(in.content_w, in.content_h, needs);
    if (v.placeholder)
        return v;
    v.show_map = static_cast<int>(v.rung) >= static_cast<int>(SizeRung::Column);
    v.show_skip_button = v.rung == SizeRung::Full || v.rung == SizeRung::Large;
    v.show_name = v.rung == SizeRung::Wide || v.rung == SizeRung::Full || v.rung == SizeRung::Large;
    return v;
}

} // namespace helix
