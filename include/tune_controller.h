// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

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

} // namespace helix::tune
