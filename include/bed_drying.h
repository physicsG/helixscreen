// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <array>
#include <optional>
#include <string_view>

/**
 * @file bed_drying.h
 * @brief The decisions behind drying filament on the heated bed, as pure
 *        functions (prestonbrown/helixscreen#1730)
 *
 * The flow is the usual heated-bed procedure: unload, clear the plate,
 * move it as far from the nozzle as the printer allows, spools on the plate
 * under a box, flip them midway, let the bed cool before removing them.
 */

namespace helix::bed_drying {

/// Enclosure answer before the user override: the printer database's flag,
/// else a configured chamber heater (an appliance heating a chamber implies a
/// chamber). A chamber sensor alone proves nothing; open printers log room
/// temperature too.
enum class EnclosureStyle { AUTO = 0, ENCLOSED = 1, OPEN = 2 };

[[nodiscard]] constexpr bool is_enclosed(EnclosureStyle style, bool db_enclosed,
                                         bool has_chamber_heater) {
    switch (style) {
    case EnclosureStyle::ENCLOSED:
        return true;
    case EnclosureStyle::OPEN:
        return false;
    case EnclosureStyle::AUTO:
    default:
        return db_enclosed || has_chamber_heater;
    }
}

/// A lying 1 kg spool is 65-75 mm tall; a cover box and margin need the rest.
inline constexpr double kMinZTravelMm = 130.0;
/// How far short of the end of Z travel the clearance move stops: room for
/// anything lying under a plate that moves down.
inline constexpr double kZClearanceMarginMm = 20.0;
/// Beyond this, spools and filament deform faster than a bed dries them.
inline constexpr int kMaxBedC = 90;
/// The remove prompt waits for the bed to read below this.
inline constexpr double kCoolDownC = 40.0;
/// Klipper's idle timeout is held to the planned end plus this, so a run
/// HelixScreen can no longer end is ended by Klipper's own TURN_OFF_HEATERS.
inline constexpr int kDeadManMarginS = 10 * 60;
/// The hold while spools lie on an unheated bed: placing them, cooling down,
/// waiting to be taken off. Nothing is hot then, so the idle timeout's only
/// effect would be its M84, which lets a gantry sink onto the spools. 24 h
/// covers a run that ends overnight and a removal the next day.
inline constexpr int kSpoolsOnBedHoldS = 24 * 3600;
/// Klipper's idle_timeout default, put back when the configured value was never read.
inline constexpr int kKlipperDefaultIdleS = 600;

/// Open printers never offer it; an owner marks a DIY enclosure through the
/// enclosure override instead.
[[nodiscard]] constexpr bool available(bool heated_bed, bool enclosed, bool has_z, double z_min,
                                       double z_max) {
    return heated_bed && enclosed && has_z && (z_max - z_min) >= kMinZTravelMm;
}

/// Absolute Z of the clearance move: the far end of travel minus a margin. On a
/// printer whose bed moves this is the plate at the bottom; on one whose gantry
/// moves it is the nozzle at the top. Either way the plate is as far from the
/// nozzle as it gets, which a cover box needs.
[[nodiscard]] constexpr double clearance_z(double z_max) {
    return z_max - kZClearanceMarginMm;
}

struct Material {
    std::string_view name;
    int bed_c; ///< upper end of the heated-bed drying range
    int air_c; ///< drying air temperature for a chamber appliance, a dryer-box figure
    int hours;
};

/// Drying values, all 12 h. The bed value is the upper end of the
/// heated-bed range; the air value is what a chamber dryer should hold, far
/// below the bed's, since the air around PLA must stay under its softening point.
inline constexpr std::array<Material, 5> kMaterials = {{
    {"PLA", 70, 50, 12},
    {"PLA Silk/CF", 75, 55, 12},
    {"PETG", 85, 65, 12},
    {"TPU", 90, 75, 12},
    {"ABS/ASA/PC/PA", 100, 80, 12},
}};

/// The bed temperature a material dries at: its table value, capped at
/// kMaxBedC and at the bed's own maximum (0 = unknown, no cap).
[[nodiscard]] constexpr int bed_temp_c(const Material& m, int bed_max_c) {
    int c = std::min(m.bed_c, kMaxBedC);
    if (bed_max_c > 0) {
        c = std::min(c, bed_max_c);
    }
    return c;
}

/// What heats the chamber alongside the bed. A dryer wins: on an appliance
/// that is both, its drying mode drives the same heater a plain target would.
enum class ChamberAssist { None, Dryer, Heater };

[[nodiscard]] constexpr ChamberAssist chamber_assist(bool has_dryer, bool has_chamber_heater) {
    if (has_dryer) {
        return ChamberAssist::Dryer;
    }
    return has_chamber_heater ? ChamberAssist::Heater : ChamberAssist::None;
}

/// The chamber target a plain heater holds: the material's air value, capped at
/// the heater's maximum (0 = unknown, no cap).
[[nodiscard]] constexpr int chamber_temp_c(const Material& m, int chamber_max_c) {
    return chamber_max_c > 0 ? std::min(m.air_c, chamber_max_c) : m.air_c;
}

/// Whether to offer the unload before anything moves.
enum class UnloadOffer {
    None,        ///< a sensor says the toolhead is empty: go straight on
    Recommended, ///< a sensor says it is loaded
    Offered,     ///< the printer cannot tell: offer beside a "make sure" line
};

/// @param toolhead_loaded nullopt when no sensor can say for certain
[[nodiscard]] constexpr UnloadOffer unload_offer(std::optional<bool> toolhead_loaded) {
    if (!toolhead_loaded.has_value()) {
        return UnloadOffer::Offered;
    }
    return *toolhead_loaded ? UnloadOffer::Recommended : UnloadOffer::None;
}

/// Where a filament-system unload the flow waits on stands. The system has to
/// be seen busy before an idle reading means it finished: the idle it reports
/// as the unload is sent is the state it started from. ERROR means it gave up,
/// and the error surface that owns that edge reports it.
enum class UnloadProgress { Waiting, Done, Failed };

/// @param seen_busy whether any earlier reading was busy
[[nodiscard]] constexpr UnloadProgress unload_progress(bool seen_busy, bool busy, bool error) {
    if (error) {
        return UnloadProgress::Failed;
    }
    return (busy || !seen_busy) ? UnloadProgress::Waiting : UnloadProgress::Done;
}

/// What the sensors say about filament at the toolhead; nullopt when none can
/// say for certain. A toolhead sensor answers both ways. A filament system
/// reporting a loaded lane, or a runout sensor seeing filament, says loaded;
/// neither can promise an empty toolhead, since the tail of a run-out spool can
/// still sit in the extruder past the runout sensor.
[[nodiscard]] constexpr std::optional<bool>
toolhead_loaded_from(std::optional<bool> toolhead_sensor, std::optional<bool> runout_sensor,
                     bool ams_loaded) {
    if (toolhead_sensor.has_value()) {
        return toolhead_sensor;
    }
    if (ams_loaded || runout_sensor.value_or(false)) {
        return true;
    }
    return std::nullopt;
}

/// Where the run stands at @p now, given its persisted start and planned end.
enum class Phase { Running, Ended };

[[nodiscard]] constexpr Phase phase_at(long long now_s, long long end_s) {
    return now_s < end_s ? Phase::Running : Phase::Ended;
}

/// The flip reminder is due once the run is half done.
[[nodiscard]] constexpr bool flip_due(long long now_s, long long start_s, long long end_s) {
    return now_s >= start_s + (end_s - start_s) / 2;
}

[[nodiscard]] constexpr bool may_prompt_removal(double bed_temp_c) {
    return bed_temp_c < kCoolDownC;
}

/// A run as persisted in settings.json. `latched` is the spools-on-the-bed
/// latch: set before any heat is sent, cleared only when the user confirms the
/// spools are out, so it survives an app restart, a Klipper restart and a
/// power loss.
struct RunRecord {
    bool latched = false;
    bool placing = false;  ///< latched while the place prompt is up; nothing heats yet
    int material = -1;     ///< index into kMaterials, so a restart mid-placement keeps the choice
    long long start_s = 0; ///< wall clock, seconds
    long long end_s = 0;   ///< planned end, wall clock, seconds
    int bed_c = 0;
    int idle_restore_s = 0; ///< the configured idle timeout to put back; 0 = none held
    bool appliance = false; ///< a chamber appliance dries alongside
    int chamber_c = 0;      ///< a plain chamber heater's target for the run; 0 = none
    bool ended = false;     ///< heaters are off; waiting for the spools to come out
    bool flip_notified = false;
};

} // namespace helix::bed_drying
