// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace helix {

/**
 * @brief How an overlay presents itself relative to the navigation dock.
 *
 * The two overlay widths registered by theme_manager are not spacing options —
 * they encode whether the user has *arrived* somewhere or has *opened something
 * on top of* where they were:
 *
 *   destination      screen - nav               occludes the backdrop entirely.
 *                                               A place you park. Drill-downs
 *                                               from it are destinations too
 *                                               (iOS push semantics: you are
 *                                               still inside Settings when you
 *                                               open Settings > Connection >
 *                                               Network).
 *
 *   transient layer  screen - nav - space_lg    the backdrop shows at the
 *                                               leading edge. Something you
 *                                               opened and will return from.
 *
 * Which one an overlay gets is a property of *how the user reached it*, not of
 * the overlay itself — fan_control_overlay is a transient layer when opened
 * from Controls and a drill-down when opened from Settings > Devices > Fans. That is why
 * the class is resolved at push time rather than baked into XML.
 *
 * See prestonbrown/helixscreen#1178.
 */
enum class OverlayClass {
    /// Take the class of whatever this overlay was pushed from.
    Inherit,
    /// Always a destination, whatever pushed it. Used by the long-dwell screens
    /// (AMS, AMS Overview, Print Status) that users park on for long stretches.
    Destination,
};

/**
 * @brief Resolve an overlay's width class at push time.
 *
 * Pure function so the rule is testable without an LVGL display. Callers supply
 * the state; NavigationManager::push_overlay() is the only production caller.
 *
 * @param requested            Class the push site asked for.
 * @param has_parent_overlay   True when another overlay is already on the stack
 *                             beneath this one.
 * @param parent_is_destination Resolved class of that parent overlay. Ignored
 *                             when @p has_parent_overlay is false.
 * @param root_is_destination  Class of the active nav root, used when this is
 *                             the first overlay on the stack. See
 *                             nav_root_is_destination().
 * @return true if the overlay renders at destination width.
 */
constexpr bool resolve_overlay_is_destination(OverlayClass requested, bool has_parent_overlay,
                                              bool parent_is_destination,
                                              bool root_is_destination) {
    if (requested == OverlayClass::Destination) {
        return true;
    }
    if (has_parent_overlay) {
        return parent_is_destination;
    }
    return root_is_destination;
}

} // namespace helix
