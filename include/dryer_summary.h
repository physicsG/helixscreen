// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The drying state of every source a printer can dry filament with, as one pure
// function. No LVGL: the model reads the live sources into DryerSummaryInput and the
// Dryer home widget renders the result.
//
// Three independent sources: box dryers (one per unit or heater, from the backend's
// environment zones), the chamber heater's drying cycle, and drying on the heated bed.

#include "ams_environment_zone.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace helix {

enum class DryerSourceKind : int { Box = 0, Chamber = 1, Bed = 2 };

/// One thing that can dry filament. Unknown numbers are -1.
struct DryerSource {
    DryerSourceKind kind = DryerSourceKind::Box;
    int unit_index = -1; ///< Box: the unit the tap opens
    std::string name; ///< Box: the backend's label (may be empty); other kinds are named by the UI
    bool drying = false;
    int progress_pct = -1; ///< 0..100; -1 draws no ring and no bar
    int remaining_min = -1;
    int current_c = -1;
    int target_c = -1;
    int humidity_pct = -1;
};

struct ChamberDryerReading {
    bool available = false;
    bool active = false;
    int remaining_min = -1;
    int total_min = -1; ///< ChamberCycle::total_min; -1 draws no ring
    int current_c = -1;
    int target_c = -1;
};

/// What the model has watched of the chamber's cycle. The appliance reports minutes left
/// and no duration, so a progress is only known for a cycle this session saw begin: its
/// total is the longest remaining time seen since. A cycle already running when the first
/// reading arrives has no known total and draws no ring.
struct ChamberCycle {
    bool seen = false;       ///< a reading has been taken
    bool was_active = false; ///< the previous reading was active
    bool counted = false;    ///< this cycle began under observation
    int total_min = -1;
};

/// @param reported a status frame from the printer has reported the state; before one the
///        "idle" reading is only the default, and the first real reading is the baseline.
[[nodiscard]] constexpr ChamberCycle next_chamber_cycle(const ChamberCycle& prev, bool reported,
                                                        bool active, int remaining_min) {
    if (!reported)
        return prev;
    ChamberCycle next;
    next.seen = true;
    if (!active)
        return next;
    next.was_active = true;
    next.counted = prev.counted || (prev.seen && !prev.was_active);
    if (next.counted) {
        const int before = prev.counted ? prev.total_min : -1;
        next.total_min = remaining_min > before ? remaining_min : before;
    }
    return next;
}

struct BedDryerReading {
    bool available = false;
    bool running = false;
    int remaining_min = -1;
    int progress_pct = -1;
    int current_c = -1;
    int target_c = -1;
};

struct DryerSummaryInput {
    std::vector<printer::EnvironmentZone> zones;
    ChamberDryerReading chamber;
    BedDryerReading bed;
};

struct DryerSummary {
    std::vector<DryerSource> sources; ///< boxes, then chamber, then bed
    int drying_count = 0;
    int highest_rh_pct = -1; ///< over zones with a dryer; -1 when none reports humidity
    bool available = false;
};

inline bool operator==(const DryerSource& a, const DryerSource& b) {
    return a.kind == b.kind && a.unit_index == b.unit_index && a.name == b.name &&
           a.drying == b.drying && a.progress_pct == b.progress_pct &&
           a.remaining_min == b.remaining_min && a.current_c == b.current_c &&
           a.target_c == b.target_c && a.humidity_pct == b.humidity_pct;
}

inline bool operator==(const DryerSummary& a, const DryerSummary& b) {
    return a.sources == b.sources && a.drying_count == b.drying_count &&
           a.highest_rh_pct == b.highest_rh_pct && a.available == b.available;
}

/// 1 - remaining / duration as a whole percent, -1 when either is unknown.
constexpr int progress_pct_from(int remaining_min, int duration_min) {
    if (remaining_min < 0 || duration_min <= 0)
        return -1;
    const int done = std::clamp(duration_min - remaining_min, 0, duration_min);
    return static_cast<int>((static_cast<long long>(done) * 100 + duration_min / 2) / duration_min);
}

[[nodiscard]] inline DryerSummary summarize_dryers(const DryerSummaryInput& in) {
    DryerSummary out;
    std::vector<std::string> seen_keys;

    for (const auto& zone : in.zones) {
        // A passive box's humidity is not the reading of anything this tile dries with.
        if (!zone.dryer.supported)
            continue;
        if (zone.env.has_humidity) {
            out.highest_rh_pct =
                std::max(out.highest_rh_pct, static_cast<int>(std::lround(zone.env.humidity_pct)));
        }
        // A unit whose sensors are per lane yields one zone per lane over one dryer.
        const std::string key = zone.heater_name.empty() ? "unit:" + std::to_string(zone.unit_index)
                                                         : "heater:" + zone.heater_name;
        if (std::find(seen_keys.begin(), seen_keys.end(), key) != seen_keys.end())
            continue;
        seen_keys.push_back(key);

        DryerSource s;
        s.kind = DryerSourceKind::Box;
        s.unit_index = zone.unit_index;
        s.name = zone.label;
        s.drying = zone.dryer.active || zone.state == printer::ZoneDryingState::Active;
        if (s.drying) {
            s.remaining_min = zone.dryer.remaining_min;
            s.progress_pct = progress_pct_from(zone.dryer.remaining_min, zone.dryer.duration_min);
            s.target_c = static_cast<int>(std::lround(zone.dryer.target_temp_c));
        }
        s.current_c = zone.env.has_temperature()
                          ? static_cast<int>(std::lround(zone.env.temperature_c))
                          : static_cast<int>(std::lround(zone.dryer.current_temp_c));
        if (zone.env.has_humidity)
            s.humidity_pct = static_cast<int>(std::lround(zone.env.humidity_pct));
        out.sources.push_back(std::move(s));
    }

    if (in.chamber.available) {
        DryerSource s;
        s.kind = DryerSourceKind::Chamber;
        s.drying = in.chamber.active;
        s.current_c = in.chamber.current_c;
        if (s.drying) {
            s.remaining_min = in.chamber.remaining_min;
            s.progress_pct = progress_pct_from(in.chamber.remaining_min, in.chamber.total_min);
            s.target_c = in.chamber.target_c;
        }
        out.sources.push_back(std::move(s));
    }

    if (in.bed.available) {
        DryerSource s;
        s.kind = DryerSourceKind::Bed;
        s.drying = in.bed.running;
        s.current_c = in.bed.current_c;
        if (s.drying) {
            s.remaining_min = in.bed.remaining_min;
            s.progress_pct = in.bed.progress_pct;
            s.target_c = in.bed.target_c;
        }
        out.sources.push_back(std::move(s));
    }

    out.drying_count = static_cast<int>(std::count_if(out.sources.begin(), out.sources.end(),
                                                      [](const auto& s) { return s.drying; }));
    out.available = !out.sources.empty();
    return out;
}

/// The source whose cycle ends first: the one a single-number rung shows. Sources with
/// no known time sort last; nullptr when nothing is drying.
[[nodiscard]] inline const DryerSource* primary_source(const DryerSummary& summary) {
    const DryerSource* best = nullptr;
    for (const auto& s : summary.sources) {
        if (!s.drying)
            continue;
        if (!best) {
            best = &s;
            continue;
        }
        const bool s_known = s.remaining_min >= 0;
        const bool best_known = best->remaining_min >= 0;
        if ((s_known && !best_known) ||
            (s_known && best_known && s.remaining_min < best->remaining_min))
            best = &s;
    }
    return best;
}

enum class DryerTapTarget : int { None = 0, Environment = 1, ChamberModal = 2, BedModal = 3 };

struct DryerTap {
    DryerTapTarget target = DryerTapTarget::None;
    int unit_index = -1; ///< Environment: the unit whose overlay opens
};

/// A tap on one source's row opens that source.
[[nodiscard]] constexpr DryerTap tap_for_source(const DryerSource& source) {
    switch (source.kind) {
    case DryerSourceKind::Box:
        return {DryerTapTarget::Environment, source.unit_index};
    case DryerSourceKind::Chamber:
        return {DryerTapTarget::ChamberModal, -1};
    case DryerSourceKind::Bed:
        return {DryerTapTarget::BedModal, -1};
    }
    return {};
}

/// A tap on the tile as a whole: the environment overlay when any box has a dryer (the
/// box ending first while one runs, else the first), otherwise the chamber's modal,
/// otherwise the bed's. Nothing starts or stops from the tile itself.
[[nodiscard]] inline DryerTap tap_for_tile(const DryerSummary& summary) {
    const DryerSource* primary = primary_source(summary);
    if (primary && primary->kind == DryerSourceKind::Box)
        return tap_for_source(*primary);
    for (const auto& s : summary.sources) {
        if (s.kind == DryerSourceKind::Box)
            return tap_for_source(s);
    }
    for (const auto& s : summary.sources) {
        if (s.kind == DryerSourceKind::Chamber)
            return tap_for_source(s);
    }
    for (const auto& s : summary.sources) {
        if (s.kind == DryerSourceKind::Bed)
            return tap_for_source(s);
    }
    return {};
}

} // namespace helix
