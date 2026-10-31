// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Where the head goes before a Z-offset calibration starts.
//
// Every calibration command takes its reading at the head's CURRENT XY:
// PROBE_CALIBRATE probes there, Z_ENDSTOP_CALIBRATE and the scan-probe
// calibrations start their paper test there. Klipper's docs ask for the head
// at Z >= 5 over the bed centre first, and a printer that is already homed
// sits wherever its last command left it (a Z tilt ends over a corner), so
// the approach is always sent, homed or not.
//
// Pure: no LVGL, no I/O.

#include "axis_move.h"
#include "printer_detector.h"
#include "printer_motion_state.h"
#include "printer_profile_state.h"
#include "probe_sensor_types.h"

#include <optional>
#include <string>

namespace helix::zoffset_cal {

/// Which part of the head the calibration reads at the starting XY.
enum class ReadingPoint {
    Nozzle, ///< Paper test at the nozzle (endstop, scan probes, manual gcode offset)
    Probe,  ///< Automatic probe at the probe's XY, then the nozzle moves over it
};

/// The command a strategy and probe run, and where it reads.
struct CalibrationCommand {
    const char* gcode; ///< Empty for FIRMWARE_MANAGED, which runs no calibrate command
    ReadingPoint point;
};

CalibrationCommand calibration_command(ZOffsetCalibrationStrategy strategy,
                                       sensors::ProbeSensorType probe_type);

/// Approach height: Klipper's minimum clearance for starting a calibration.
inline constexpr double kApproachZMm = 5.0;

struct ApproachInputs {
    bool all_homed = false;
    AxisBounds plate;        ///< preset_area(), G-code space
    AxisBounds gcode_travel; ///< PrinterMotionState::get_gcode_axis_bounds()
    bool circular_bed = false;
    ReadingPoint point = ReadingPoint::Nozzle;
    std::optional<ProbeXYOffset> probe_offset; ///< nullopt reads as a probe at the nozzle
};

struct Approach {
    /// Homing when needed, then the lift and XY move. Every line ends in '\n'.
    std::string gcode;
    /// Nozzle target in G-code space; nullopt when the plate is unknown, in
    /// which case no lift or move is in `gcode`.
    std::optional<AxisTarget> nozzle;
};

/**
 * @brief G-code that homes if needed and puts the reading point over the plate centre
 *
 * The plate centre is motion_preset_target(Center). For ReadingPoint::Probe the
 * nozzle goes to centre minus the probe offset, so the probe lands on centre.
 * The nozzle target is clamped into G-code travel, which only moves the probe
 * off centre when its offset reaches past the edge of travel.
 */
Approach calibration_approach(const ApproachInputs& in);

} // namespace helix::zoffset_cal
