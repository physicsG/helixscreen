// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>

class IMoonrakerAPI;

namespace helix::tune {

/// The single agreed clamp for speed and flow overrides.
///
/// Before this existed, PrintTuneOverlay clamped speed to [50,200] and flow to
/// [75,125] while a dead ControlsPanel path used [10,200] and [50,150]. These
/// are the shipped values — the ones users have actually been getting.
inline constexpr int kSpeedMinPct = 50;
inline constexpr int kSpeedMaxPct = 200;
inline constexpr int kFlowMinPct = 75;
inline constexpr int kFlowMaxPct = 125;

int clamp_speed_percent(int pct);
int clamp_flow_percent(int pct);

/// Clamp and send M220. No-op when `api` is null. Errors surface via NOTIFY_ERROR.
void set_speed_percent(IMoonrakerAPI* api, int pct);

/// Clamp and send M221. No-op when `api` is null. Errors surface via NOTIFY_ERROR.
void set_flow_percent(IMoonrakerAPI* api, int pct);

/// Volumetric flow for a live extruder velocity given in centi-mm/s, the unit
/// PrinterState publishes it in, through filament of the given diameter.
double volumetric_flow_mm3_s(int extruder_velocity_centimm_s, float filament_diameter_mm);

struct SpeedFlowText {
    std::string speed;
    std::string flow;
};

/// A Speed/Flow readout pair: the override percentages, or the measured
/// toolhead speed and live volumetric flow. Measured rather than commanded
/// speed, because the commanded feed rate holds its last value while the
/// toolhead sits still.
SpeedFlowText status_speed_flow_text(bool physical_units, int speed_pct, int flow_pct,
                                     int live_velocity_mm_s, int extruder_velocity_centimm_s,
                                     float filament_diameter_mm);

} // namespace helix::tune
