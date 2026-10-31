// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "axis_move.h"
#include "bed_coord_mapper.h"
#include "printer_detector.h"
#include "printer_motion_state.h"
#include "printer_state.h"

#include <optional>
#include <string_view>

namespace helix {

/// The nine fixed bed positions the Move tab offers as a 3x3 grid. Rear is
/// +Y, Front is -Y, Left is -X, Right is +X, all in G-code space.
enum class MotionPreset {
    RearLeft,
    Rear,
    RearRight,
    Left,
    Center,
    Right,
    FrontLeft,
    Front,
    FrontRight,
};

/// A preset and its stable token, as the Move tab's XML and the Toolhead widget's config
/// name it.
struct MotionPresetToken {
    MotionPreset preset;
    const char* token;
};

/// Every preset once, in grid order: the rear row, then the middle, then the front, each
/// left to right.
inline constexpr MotionPresetToken kMotionPresetTokens[] = {
    {MotionPreset::RearLeft, "rear_left"},     {MotionPreset::Rear, "rear"},
    {MotionPreset::RearRight, "rear_right"},   {MotionPreset::Left, "left"},
    {MotionPreset::Center, "center"},          {MotionPreset::Right, "right"},
    {MotionPreset::FrontLeft, "front_left"},   {MotionPreset::Front, "front"},
    {MotionPreset::FrontRight, "front_right"},
};

/// The preset's token ("rear_left" ... "front_right").
[[nodiscard]] const char* motion_preset_token(MotionPreset preset);
/// The preset a token names; nullopt for anything else.
[[nodiscard]] std::optional<MotionPreset> motion_preset_from_token(std::string_view token);

/**
 * @brief The area Move-tab presets aim at, in G-code space
 *
 * The plate the config declares ([bed_mesh] mesh_min/max) when there is one,
 * clipped to travel; otherwise the whole travel. Axis travel can run past the
 * plate into tool docks or a purge area, which a preset must never target.
 *
 * @param machine_travel toolhead axis limits (PrinterState::get_axis_bounds())
 * @param gcode_travel the same limits in G-code space
 *        (PrinterState::get_gcode_axis_bounds())
 * @param volume build volume holding the declared plate (machine space)
 */
AxisBounds preset_area(const AxisBounds& machine_travel, const AxisBounds& gcode_travel,
                       const BuildVolume& volume);

/**
 * @brief Bed position a Move-tab preset names, in G-code millimetres
 *
 * X and Y only; a preset never commands Z. On a rectangular bed, edge
 * presets sit 10% of that axis's span in from the edge and Center is the
 * midpoint. On a circular bed (delta), the eight rim presets sit at 90% of
 * the inscribed radius in their direction - a round bed has no corners, so
 * the diagonal presets sit at 45 degrees instead.
 *
 * @param gcode_bounds The area to place presets in, G-code space
 *        (preset_area()).
 * @param circular_bed True on a delta/rotary_delta machine (see
 *        circular_bed_kinematics() in printer_state.h).
 * @return nullopt when either axis's bounds are unknown or degenerate; the
 *         caller must skip the move rather than aim at fabricated
 *         coordinates.
 */
std::optional<AxisTarget> motion_preset_target(MotionPreset preset, const AxisBounds& gcode_bounds,
                                               bool circular_bed);

/// How far inside the rear edge of the plate a park position sits.
inline constexpr double kParkRearMarginMm = 10.0;

/**
 * @brief Where the toolhead parks when nothing else says: over the rear of
 *        the plate, centred in X, in G-code millimetres
 *
 * X and Y only. It never leaves the plate: kParkRearMarginMm inside the rear
 * edge, or the plate's centre line on a plate shallower than twice that.
 *
 * @param area The plate in G-code space (preset_area()).
 * @return nullopt when either axis's bounds are unknown or degenerate.
 */
std::optional<AxisTarget> plate_rear_park(const AxisBounds& area);

/**
 * @brief Mapper for the Bed tab's top-down plate drawn into a viewport
 *
 * The plate spans exactly @p area, so the area's minimum corner is the
 * mapper's origin: a centre-origin delta and a plate inset from travel map
 * the same way.
 */
BedCoordMapper bed_map_mapper(const AxisBounds& area, int viewport_w_px, int viewport_h_px);

/**
 * @brief XY target for a touch on the Bed tab, in G-code millimetres
 *
 * Never fails on a touch outside the plate: it clamps to the nearest point
 * the plate allows, which on a circular bed is the inscribed circle's rim.
 * X and Y only.
 *
 * @param x_px, y_px Touch point relative to the viewport @p mapper was built for.
 * @param area The plate in G-code space (preset_area()).
 * @return nullopt when either axis's bounds are unknown or degenerate.
 */
std::optional<AxisTarget> bed_map_target(float x_px, float y_px, const BedCoordMapper& mapper,
                                         const AxisBounds& area, bool circular_bed);

/// The least clearance, in millimetres, the top of z_max must leave above a part on the
/// bed for a lift to count as clearing it.
inline constexpr double kPartClearanceMinMm = 1.0;

/// What an XY move from the plate or a position preset does with Z first.
struct PlateLift {
    enum class Kind {
        None,   ///< already at or above the floor: travel at the current Z
        Lift,   ///< rise to `z`, then travel
        Refuse, ///< z_max cannot clear the part on the bed: do not move
    };
    Kind kind = Kind::None;
    double z = 0.0; ///< the height to rise to; meaningful for Lift only
};

/**
 * @brief The lowest Z an XY move may travel at
 *
 * @p clearance_mm with nothing on the bed; @p part_height_mm + @p clearance_mm while a
 * part may still be on it. Not capped: plate_lift() decides what z_max does to it.
 */
double plate_z_floor(double clearance_mm, std::optional<double> part_height_mm);

/**
 * @brief Decide the lift before an XY move from the plate or a position preset
 *
 * Lift to plate_z_floor() capped at @p z_max when below it; None when @p current_z is
 * already within AxisMove::kEpsilonMm of that height or above. Refuse when a part is on
 * the bed and @p z_max is less than kPartClearanceMinMm above its height.
 */
PlateLift plate_lift(double current_z, double clearance_mm, double z_max,
                     std::optional<double> part_height_mm);

/**
 * @brief Clamp a downward Z target so it never goes below the floor while a part is on the bed
 *
 * Returns @p target_z unchanged when the bed is not @p occupied or the target is at or above
 * plate_z_floor(); the floor otherwise. An occupied bed of unknown height (@p part_height_mm
 * nullopt) floors at the clearance. Never raises a target that is above the floor.
 */
double clamp_z_down_to_floor(double target_z, double clearance_mm, bool occupied,
                             std::optional<double> part_height_mm);

/**
 * @brief Half the length of a Bed-tab crosshair guide
 *
 * A guide runs edge to edge across the plate through the marker. On a
 * rectangular plate that is the full @p half_extent; on a round one it is the
 * chord at @p offset from the centre, and nothing once the marker is off the
 * plate.
 *
 * @param offset The guide's distance from the plate centre, across it (px).
 * @param half_extent Half the plate's size along the guide (px); the radius
 *        on a round plate.
 */
float bed_map_guide_half_span(float offset, float half_extent, bool circular);

} // namespace helix
