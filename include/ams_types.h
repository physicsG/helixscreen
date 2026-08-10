// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "filament_database.h"

#include <algorithm>
#include <any>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

/**
 * @file ams_types.h
 * @brief Data structures for multi-filament/AMS system support
 *
 * Supports both Happy Hare (MMU) and AFC-Klipper-Add-On systems.
 * These structures are platform-agnostic - backends translate from
 * their specific APIs to these common types.
 *
 * @note Thread Safety: These structures are NOT thread-safe. The AmsState
 * class provides thread-safe access through LVGL subjects. Direct mutation
 * of these structures should only occur in the backend layer.
 */

/// Default color for slots without filament info (medium gray)
constexpr uint32_t AMS_DEFAULT_SLOT_COLOR = 0x808080;

/**
 * @brief Type of AMS system detected
 *
 * Supports both filament-switching systems (MMU/AMS) and physical tool changers.
 * Tool changers differ in that each "slot" is a complete toolhead with its own
 * extruder, rather than a filament path to a shared toolhead.
 */
enum class AmsType {
    NONE = 0,         ///< No AMS detected
    HAPPY_HARE = 1,   ///< Happy Hare MMU (mmu object in Moonraker)
    AFC = 2,          ///< AFC-Klipper-Add-On (afc object, lane_data database)
    ACE = 3,          ///< AnyCubic ACE Pro (ValgACE/BunnyACE/DuckACE Klipper drivers)
    TOOL_CHANGER = 4, ///< Physical tool changer (viesturz/klipper-toolchanger)
    AD5X_IFS = 5,     ///< FlashForge AD5X IFS (Intelligent Filament Switching)
    CFS = 6,          ///< Creality Filament System (K2 series, RS-485)
    SNAPMAKER = 7,    ///< Snapmaker U1 SnapSwap toolchanger
    QIDI_BOX = 8, ///< QIDI Box filament changer (PLUS4, Q2, MAX4 — hub AMS, 4 slots chainable to 16)
    /// multiACE (decay71/multiACE): 1-4 Anycubic ACE Pro / ACE 2 units bolted onto a
    /// Snapmaker U1's four toolheads. Registers a Klipper object literally named `ace`,
    /// which is ALSO the community Anycubic driver's name — see PrinterDiscovery for how
    /// the two are told apart. Superset of SNAPMAKER: the U1's own heads plus the ACE units.
    MULTIACE = 9
};

/**
 * @brief Get string name for AMS type
 * @param type The AMS type enum value
 * @return Human-readable string for the type
 */
inline const char* ams_type_to_string(AmsType type) {
    switch (type) {
    case AmsType::HAPPY_HARE:
        return "Happy Hare";
    case AmsType::AFC:
        return "AFC";
    case AmsType::ACE:
        return "ACE";
    case AmsType::TOOL_CHANGER:
        return "Tool Changer";
    case AmsType::AD5X_IFS:
        return "AD5X IFS";
    case AmsType::CFS:
        return "CFS";
    case AmsType::SNAPMAKER:
        return "Snapmaker";
    case AmsType::QIDI_BOX:
        return "QIDI Box"; // i18n: do not translate - product name
    case AmsType::MULTIACE:
        return "multiACE"; // i18n: do not translate - product name
    default:
        return "None";
    }
}

/**
 * @brief Parse AMS type from string (for Moonraker responses)
 * @param str String to parse (case-insensitive)
 * @return Matching AmsType or NONE if not recognized
 */
inline AmsType ams_type_from_string(std::string_view str) {
    // Simple comparison - backends will use their own detection
    if (str == "mmu" || str == "happy_hare" || str == "Happy Hare") {
        return AmsType::HAPPY_HARE;
    }
    if (str == "afc" || str == "AFC") {
        return AmsType::AFC;
    }
    if (str == "valgace" || str == "ValgACE" || str == "bunnyace" || str == "BunnyACE" ||
        str == "duckace" || str == "DuckACE" || str == "ace" || str == "ACE Pro") {
        return AmsType::ACE;
    }
    if (str == "toolchanger" || str == "tool_changer" || str == "Tool Changer") {
        return AmsType::TOOL_CHANGER;
    }
    if (str == "ad5x_ifs" || str == "ad5x ifs" || str == "ad5x" || str == "ifs") {
        return AmsType::AD5X_IFS;
    }
    if (str == "cfs" || str == "CFS" || str == "box") {
        return AmsType::CFS;
    }
    if (str == "snapmaker" || str == "Snapmaker" || str == "snapswap" || str == "SnapSwap") {
        return AmsType::SNAPMAKER;
    }
    // QIDI Box: QIDI's 4-slot hub-style filament changer (PLUS4 / Q2 / MAX4).
    // Note: the bare "box" alias is already claimed by CFS above, so QIDI Box
    // requires the explicit "qidi_box" / "QIDI Box" spelling.
    if (str == "qidi_box" || str == "QIDI Box" || str == "qidibox") {
        return AmsType::QIDI_BOX;
    }
    return AmsType::NONE;
}

/**
 * @brief Check if AMS type is a physical tool changer
 *
 * Tool changers have fundamentally different behavior than filament systems:
 * - Each "slot" is a complete toolhead with its own extruder
 * - Path topology is PARALLEL (not converging to a single nozzle)
 * - "Loading" means mounting the tool, not feeding filament
 *
 * @param type The AMS type to check
 * @return true if this is a physical tool changer
 */
inline bool is_tool_changer(AmsType type) {
    // MULTIACE is a Snapmaker U1 with ACE units bolted on — still four physical
    // toolheads, so every tool-changer behaviour applies exactly as it does to
    // SNAPMAKER.
    return type == AmsType::TOOL_CHANGER || type == AmsType::SNAPMAKER ||
           type == AmsType::MULTIACE;
}

/**
 * @brief Check if AMS type is a filament-switching system
 *
 * Filament systems route multiple filaments to a single toolhead:
 * - Happy Hare, AFC, ACE all fall into this category
 * - Path topology is LINEAR or HUB (converging to single nozzle)
 *
 * @param type The AMS type to check
 * @return true if this is a filament-switching system
 */
inline bool is_filament_system(AmsType type) {
    return type == AmsType::HAPPY_HARE || type == AmsType::AFC || type == AmsType::ACE ||
           type == AmsType::AD5X_IFS || type == AmsType::CFS || type == AmsType::SNAPMAKER ||
           type == AmsType::QIDI_BOX || type == AmsType::MULTIACE;
}

/**
 * @brief Slot/Lane status
 *
 * Our internal status representation. Use conversion functions to
 * translate from Happy Hare's gate_status values (-1, 0, 1, 2).
 */
enum class SlotStatus {
    UNKNOWN = 0,     ///< Status not known
    EMPTY = 1,       ///< No filament in slot
    AVAILABLE = 2,   ///< Filament available, not loaded
    LOADED = 3,      ///< Filament loaded to extruder
    FROM_BUFFER = 4, ///< Filament available from buffer
    BLOCKED = 5      ///< Slot blocked/jammed
};

/**
 * @brief Get string name for slot status
 * @param status The slot status enum value
 * @return Human-readable string for the status
 */
inline const char* slot_status_to_string(SlotStatus status) {
    switch (status) {
    case SlotStatus::EMPTY:
        return "Empty";
    case SlotStatus::AVAILABLE:
        return "Available";
    case SlotStatus::LOADED:
        return "Loaded";
    case SlotStatus::FROM_BUFFER:
        return "From Buffer";
    case SlotStatus::BLOCKED:
        return "Blocked";
    default:
        return "Unknown";
    }
}

/**
 * @brief Convert Happy Hare gate_status integer to SlotStatus enum
 *
 * Happy Hare uses: -1 = unknown, 0 = empty, 1 = available, 2 = from buffer
 * The "loaded" state is determined by comparing with current_slot, not from
 * gate_status directly.
 *
 * @param hh_status Happy Hare gate_status value (-1, 0, 1, or 2)
 * @return Corresponding SlotStatus enum value
 */
inline SlotStatus slot_status_from_happy_hare(int hh_status) {
    switch (hh_status) {
    case -1:
        return SlotStatus::UNKNOWN;
    case 0:
        return SlotStatus::EMPTY;
    case 1:
        return SlotStatus::AVAILABLE;
    case 2:
        return SlotStatus::FROM_BUFFER;
    default:
        return SlotStatus::UNKNOWN;
    }
}

/**
 * @brief Convert SlotStatus enum to Happy Hare gate_status integer
 * @param status Our SlotStatus enum value
 * @return Happy Hare gate_status value (-1, 0, 1, or 2)
 */
inline int slot_status_to_happy_hare(SlotStatus status) {
    switch (status) {
    case SlotStatus::UNKNOWN:
        return -1;
    case SlotStatus::EMPTY:
        return 0;
    case SlotStatus::AVAILABLE:
        return 1;
    case SlotStatus::FROM_BUFFER:
        return 2;
    // LOADED and BLOCKED don't have direct HH equivalents
    case SlotStatus::LOADED:
        return 1; // Treat as available
    case SlotStatus::BLOCKED:
        return -1; // Treat as unknown
    default:
        return -1;
    }
}

/**
 * @brief Current AMS action/operation
 *
 * Maps to Happy Hare's action strings:
 * "Idle", "Loading", "Unloading", "Forming Tip", "Cutting", "Heating", etc.
 */
enum class AmsAction {
    IDLE = 0,        ///< No operation in progress
    LOADING = 1,     ///< Loading filament to extruder
    UNLOADING = 2,   ///< Unloading filament from extruder
    SELECTING = 3,   ///< Selecting tool/slot
    RESETTING = 4,   ///< Resetting system (MMU_HOME for HH, AFC_RESET for AFC)
    FORMING_TIP = 5, ///< Forming filament tip (legacy, some systems still use)
    HEATING = 6,     ///< Heating for operation
    CHECKING = 7,    ///< Internal sensor verification (not shown in UI)
    PAUSED = 8,      ///< Operation paused (requires attention)
    ERROR = 9,       ///< Error state
    CUTTING = 10,    ///< Cutting filament before retraction (modern AMS)
    PURGING = 11     ///< Purging old filament color after load
};

/**
 * @brief Get string name for AMS action
 * @param action The action enum value
 * @return Human-readable string for the action
 */
inline const char* ams_action_to_string(AmsAction action) {
    switch (action) {
    case AmsAction::IDLE:
        return "Idle";
    case AmsAction::LOADING:
        return "Loading";
    case AmsAction::UNLOADING:
        return "Unloading";
    case AmsAction::SELECTING:
        return "Selecting";
    case AmsAction::RESETTING:
        return "Resetting";
    case AmsAction::FORMING_TIP:
        return "Forming Tip";
    case AmsAction::CUTTING:
        return "Cutting";
    case AmsAction::HEATING:
        return "Heating";
    case AmsAction::CHECKING:
        return "Checking";
    case AmsAction::PAUSED:
        return "Paused";
    case AmsAction::ERROR:
        return "Error";
    case AmsAction::PURGING:
        return "Purging";
    default:
        return "Unknown";
    }
}

/**
 * @brief Normalize a firmware state string to a comparison token
 *
 * Lowercases and drops every non-alphanumeric character, so "Tool swap",
 * "ToolSwap", "TOOL_SWAP" and "tool-swap" all collapse to "toolswap".
 *
 * Firmware state vocabularies get reworded without notice — AFC renamed its
 * TOOL_SWAP value from "Tool swap" to "ToolSwap" in v1.2.0. Comparing
 * normalized tokens makes that entire class of rename a non-event instead of a
 * silent fall-through to IDLE.
 */
inline std::string ams_normalize_state_token(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s) {
        if (std::isalnum(c) != 0) {
            out.push_back(static_cast<char>(std::tolower(c)));
        }
    }
    return out;
}

/**
 * @brief Parse AMS action from a firmware action/state string
 *
 * Handles both Happy Hare (`printer.mmu.action`) and AFC
 * (`printer.AFC.current_state`) vocabularies. Matching is done on the
 * normalized token (see ams_normalize_state_token), then falls back to ordered
 * substring matching so a reworded or previously-unseen state still resolves to
 * a sensible action rather than silently reading as IDLE.
 *
 * @param action_str    Raw state string from the firmware
 * @param out_recognized Optional; set true when the exact token is in our known
 *                       vocabulary, false when the result came from the fuzzy
 *                       fallback or no rule matched. Callers use this to log
 *                       schema drift once per unseen string. An empty input
 *                       reports true — "no state" is normal, not drift.
 * @return Corresponding AmsAction enum value
 */
inline AmsAction ams_action_from_string(std::string_view action_str,
                                        bool* out_recognized = nullptr) {
    auto recognize = [&](bool ok) {
        if (out_recognized != nullptr)
            *out_recognized = ok;
    };

    const std::string t = ams_normalize_state_token(action_str);
    if (t.empty()) {
        recognize(true);
        return AmsAction::IDLE;
    }

    struct Entry {
        const char* token;
        AmsAction action;
    };
    // Known vocabulary, normalized. Happy Hare and AFC share this table; a
    // token appearing in only one firmware is harmless to the other.
    static constexpr Entry kExact[] = {
        {"idle", AmsAction::IDLE},
        {"initialized", AmsAction::IDLE},
        {"loading", AmsAction::LOADING},
        {"loadingext", AmsAction::LOADING},
        {"unloading", AmsAction::UNLOADING},
        {"exitingext", AmsAction::UNLOADING},
        {"ejecting", AmsAction::UNLOADING},
        {"selecting", AmsAction::SELECTING},
        // AFC toolchanger states (v1.2.0 #768). "Tool swap" was the pre-1.2.0
        // spelling; both normalize to "toolswap".
        {"toolswap", AmsAction::SELECTING},
        {"tooldock", AmsAction::SELECTING},
        {"toolpickup", AmsAction::SELECTING},
        {"moving", AmsAction::SELECTING},
        {"restoring", AmsAction::SELECTING},
        {"homing", AmsAction::RESETTING},
        {"resetting", AmsAction::RESETTING},
        {"formingtip", AmsAction::FORMING_TIP},
        {"cutting", AmsAction::CUTTING},
        {"cuttingtip", AmsAction::CUTTING},
        {"cuttingfilament", AmsAction::CUTTING},
        {"heating", AmsAction::HEATING},
        {"checking", AmsAction::CHECKING},
        {"purging", AmsAction::PURGING},
        {"paused", AmsAction::PAUSED},
        {"error", AmsAction::ERROR},
    };
    for (const auto& e : kExact) {
        if (t == e.token) {
            recognize(true);
            return e.action;
        }
    }

    // Fuzzy fallback. Reaching here means the exact string is new to us, so
    // report it as unrecognized even when a rule below matches — the caller
    // logs it once and we still behave sensibly in the meantime.
    recognize(false);
    auto has = [&](const char* needle) { return t.find(needle) != std::string::npos; };

    // Order is load-bearing: "unloading" contains "loading", so unload-ish
    // words must be tested first. Error and pause outrank motion words because
    // an errored move is an error, not a move.
    if (has("error"))
        return AmsAction::ERROR;
    if (has("pause"))
        return AmsAction::PAUSED;
    if (has("unload") || has("eject") || has("exiting"))
        return AmsAction::UNLOADING;
    if (has("toolswap") || has("tooldock") || has("toolpickup") || has("toolchange") || has("swap"))
        return AmsAction::SELECTING;
    if (has("load"))
        return AmsAction::LOADING;
    if (has("select"))
        return AmsAction::SELECTING;
    if (has("purg"))
        return AmsAction::PURGING;
    if (has("cut"))
        return AmsAction::CUTTING;
    if (has("heat"))
        return AmsAction::HEATING;
    if (has("tip"))
        return AmsAction::FORMING_TIP;
    if (has("home") || has("reset"))
        return AmsAction::RESETTING;
    if (has("restor") || has("moving"))
        return AmsAction::SELECTING;
    if (has("check"))
        return AmsAction::CHECKING;
    if (has("idle") || has("initial"))
        return AmsAction::IDLE;
    return AmsAction::IDLE;
}

// ============================================================================
// Tip Handling Method
// ============================================================================

/**
 * @brief How the AMS handles filament tip during unload
 *
 * Different systems use different methods to prepare filament for retraction:
 * - CUT: Physical cutter severs filament cleanly (Happy Hare with cutter, AFC)
 * - TIP_FORM: Heat+retract sequence forms a tapered tip (Bambu AMS, some HH configs)
 * - NONE: System doesn't actively manage tip (manual, or no retraction support)
 */
enum class TipMethod {
    NONE = 0,    ///< No active tip handling
    CUT = 1,     ///< Physical filament cutter
    TIP_FORM = 2 ///< Heat and retract to form tapered tip
};

/**
 * @brief Get string name for tip method
 * @param method The tip method enum value
 * @return Human-readable string for the method
 */
inline const char* tip_method_to_string(TipMethod method) {
    switch (method) {
    case TipMethod::NONE:
        return "None";
    case TipMethod::CUT:
        return "Cutter";
    case TipMethod::TIP_FORM:
        return "Tip Forming";
    }
    return "Unknown";
}

/**
 * @brief Get user-friendly step label for tip handling
 * @param method The tip method enum value
 * @return Label suitable for step progress display
 */
inline const char* tip_method_step_label(TipMethod method) {
    switch (method) {
    case TipMethod::CUT:
        return "Cut & retract";
    case TipMethod::TIP_FORM:
        return "Form tip & retract";
    case TipMethod::NONE:
    default:
        return "Retract";
    }
}

// ============================================================================
// Filament Path Visualization Types
// ============================================================================

/**
 * @brief Path topology - affects visual rendering of the filament path
 *
 * Different multi-material systems have different physical topologies:
 * - LINEAR: Selector picks one input from multiple gates (Happy Hare ERCF)
 * - HUB: Multiple lanes merge into a common hub/merger (AFC Box Turtle)
 * - PARALLEL: Each input has its own independent path to a separate toolhead
 *             (physical tool changers like StealthChanger/TapChanger)
 */
enum class PathTopology {
    LINEAR = 0,   ///< Happy Hare: selector picks one input
    HUB = 1,      ///< AFC: merger combines inputs through hub
    PARALLEL = 2, ///< Tool Changer: each slot is a separate toolhead
    MIXED = 3     ///< Direct + Hub: some lanes direct, some through hub to shared extruder
};

/**
 * @brief Whether a toolhead is currently on the carriage.
 *
 * Distinct from "is any filament loaded". On a toolchanger every parked toolhead
 * may hold filament while the shuttle carries none — a state `current_slot = -1`
 * ("nothing loaded anywhere") cannot express. Without it, something is always
 * elected current, and on #1229's machine the election latched onto a parked
 * lane and never moved again.
 *
 * Only meaningful for backends that actually have a carriage. Single-extruder
 * systems leave this UNKNOWN and are unaffected.
 */
enum class MountState {
    UNKNOWN = 0,  ///< No carriage, or no signal yet — do not draw conclusions
    NONE = 1,     ///< Carriage is empty. Parked filament does not make a slot current
    CHANGING = 2, ///< Mid-toolchange; sources legitimately disagree, elect nothing
    MOUNTED = 3   ///< A tool is on the carriage; see AmsSystemInfo::mounted_tool
};

/// Human-readable name for a MountState, for logs and diagnostics.
inline const char* mount_state_to_string(MountState state) {
    switch (state) {
    case MountState::NONE:
        return "none";
    case MountState::CHANGING:
        return "changing";
    case MountState::MOUNTED:
        return "mounted";
    case MountState::UNKNOWN:
    default:
        return "unknown";
    }
}

/**
 * @brief Get string name for path topology
 * @param topology The topology enum value
 * @return Human-readable string for the topology
 */
inline const char* path_topology_to_string(PathTopology topology) {
    switch (topology) {
    case PathTopology::LINEAR:
        return "Linear (Selector)";
    case PathTopology::HUB:
        return "Hub (Merger)";
    case PathTopology::PARALLEL:
        return "Parallel (Tool Changer)";
    case PathTopology::MIXED:
        return "Mixed (Direct + Hub)";
    default:
        return "Unknown";
    }
}

/**
 * @brief Unified path segments (AFC-inspired naming)
 *
 * Both Happy Hare and AFC map to these same logical segments. The path
 * canvas widget draws them differently based on PathTopology.
 *
 * Physical filament path (top to bottom in UI):
 *   SPOOL → PREP → LANE → HUB → OUTPUT → TOOLHEAD → NOZZLE
 *
 * Happy Hare mapping:
 *   SPOOL=Gate storage, PREP=Gate sensor, LANE=Gate-to-selector,
 *   HUB=Selector, OUTPUT=Bowden tube, TOOLHEAD=Extruder sensor, NOZZLE=Loaded
 *
 * AFC mapping:
 *   SPOOL=Lane spool, PREP=Prep sensor, LANE=Lane tube,
 *   HUB=Hub/Merger, OUTPUT=Output tube, TOOLHEAD=Toolhead sensor, NOZZLE=Loaded
 */
enum class PathSegment {
    NONE = 0,     ///< No segment / idle / filament not present
    SPOOL = 1,    ///< At spool (filament storage area)
    PREP = 2,     ///< At entry sensor (prep/gate sensor)
    LANE = 3,     ///< In lane/gate-to-router segment
    HUB = 4,      ///< At router (selector or hub/merger)
    OUTPUT = 5,   ///< In output tube (bowden or hub output)
    TOOLHEAD = 6, ///< At toolhead sensor
    NOZZLE = 7    ///< Fully loaded in nozzle
};

/// Number of path segments for iteration (NONE through NOZZLE)
constexpr int PATH_SEGMENT_COUNT = 8;

/**
 * @brief Get string name for path segment
 * @param segment The segment enum value
 * @return Human-readable string for the segment
 */
inline const char* path_segment_to_string(PathSegment segment) {
    switch (segment) {
    case PathSegment::NONE:
        return "None";
    case PathSegment::SPOOL:
        return "Spool";
    case PathSegment::PREP:
        return "Prep Sensor";
    case PathSegment::LANE:
        return "Lane";
    case PathSegment::HUB:
        return "Hub/Selector";
    case PathSegment::OUTPUT:
        return "Output Tube";
    case PathSegment::TOOLHEAD:
        return "Toolhead";
    case PathSegment::NOZZLE:
        return "Nozzle";
    default:
        return "Unknown";
    }
}

/**
 * @brief Convert Happy Hare filament_pos to unified PathSegment
 *
 * Happy Hare filament_pos values:
 *   0 = unloaded (at spool)
 *   1 = homed at gate
 *   2 = in gate
 *   3 = in bowden
 *   4 = end of bowden
 *   5 = homed at extruder
 *   6 = extruder entry
 *   7 = in extruder
 *   8 = fully loaded
 *
 * @param filament_pos Happy Hare filament_pos value
 * @return Corresponding PathSegment
 */
inline PathSegment path_segment_from_happy_hare_pos(int filament_pos) {
    switch (filament_pos) {
    case 0:
        return PathSegment::SPOOL;
    case 1:
    case 2:
        return PathSegment::PREP; // Gate area
    case 3:
        return PathSegment::LANE; // Moving through
    case 4:
        return PathSegment::HUB; // At selector
    case 5:
        return PathSegment::OUTPUT; // In bowden
    case 6:
        return PathSegment::TOOLHEAD; // At extruder
    case 7:
    case 8:
    case 9: // v4: in-nozzle positions
    case 10:
        return PathSegment::NOZZLE; // Loaded
    default:
        return PathSegment::NONE;
    }
}

/**
 * @brief Infer PathSegment from AFC sensor states
 *
 * AFC uses binary sensor states to determine filament position.
 * Logic: filament is at or past the last sensor that detects it.
 *
 * @param prep_sensor Prep sensor triggered (filament at lane entry)
 * @param hub_sensor Hub sensor triggered (filament in hub)
 * @param toolhead_sensor Toolhead sensor triggered (filament at extruder)
 * @return Inferred PathSegment based on sensor states
 */
inline PathSegment path_segment_from_afc_sensors(bool prep_sensor, bool hub_sensor,
                                                 bool toolhead_sensor) {
    if (toolhead_sensor)
        return PathSegment::NOZZLE;
    if (hub_sensor)
        return PathSegment::TOOLHEAD; // Past hub, approaching toolhead
    if (prep_sensor)
        return PathSegment::HUB; // Past prep, approaching hub
    return PathSegment::SPOOL;   // Not yet at prep
}

/**
 * @brief Per-slot error state
 *
 * Populated by backends when a slot/lane enters an error condition.
 * AFC populates from per-lane status; Happy Hare maps system-level
 * errors to the active gate.
 */
struct SlotError {
    std::string message;                                     ///< Human-readable error description
    enum Severity { INFO, WARNING, ERROR } severity = ERROR; ///< Error severity level
};

/**
 * @brief Environmental sensor data (temperature + humidity)
 *
 * Used on AmsUnit and SlotInfo as std::optional<EnvironmentData>.
 * CFS provides per-unit environment; future backends may provide per-slot.
 */
struct EnvironmentData {
    float temperature_c = 0.0f; ///< Temperature in Celsius
    float humidity_pct = 0.0f;  ///< Relative humidity percentage (0-100)
    bool has_humidity = false;  ///< true when backend provides humidity sensor
};

/// Error targeting level for multi-level error reporting
enum class AmsAlertLevel { SLOT, UNIT, SYSTEM };

/**
 * @brief Active error/warning with human-readable message and troubleshooting hint
 *
 * Different from AmsError (in ams_error.h, operation result type) — this represents
 * a persistent alert condition reported by the backend hardware.
 */
struct AmsAlert {
    std::string message;    ///< Human-readable error message
    std::string hint;       ///< Actionable troubleshooting text
    std::string error_code; ///< Backend-specific code: "CFS-843", "AFC-LANE_ERROR"
    AmsAlertLevel level = AmsAlertLevel::SYSTEM;
    SlotError::Severity severity = SlotError::Severity::ERROR;
    int unit_index = -1; ///< For UNIT/SLOT level (-1 = N/A)
    int slot_index = -1; ///< For SLOT level (-1 = N/A)
};

/**
 * @brief Buffer health data for AFC buffer fault detection
 *
 * Populated from AFC_buffer status objects. Only applicable to AFC
 * systems with TurtleNeck buffer hardware. Other backends leave
 * buffer_health as nullopt on SlotInfo.
 */
struct BufferHealth {
    bool fault_detection_enabled = false; ///< Whether buffer fault detection is active
    float distance_to_fault = -1.0f;      ///< Distance to fault in mm (-1 = not tracking/null)
    float error_sensitivity = 0.0f;       ///< AFC sensitivity (1-10), 0 = not reported
    std::string state;                    ///< Buffer state (e.g., "Advancing", "Trailing")

    /// Lane the buffer is currently regulating. AFC only names one while the
    /// buffer is enabled AND a lane is loaded; empty otherwise (AFC v1.2.0+).
    std::string active_lane;

    /// Rotation-distance trim the buffer is driving on the active lane's stepper.
    /// AFC reports this only while enabled with a lane loaded; -1 = not reported.
    float rotation_distance = -1.0f;

    /// Rotation-distance multiplier the buffer last applied, and the configured
    /// bounds it swings between (`multiplier_high`/`multiplier_low` in the AFC
    /// buffer config). -1 = not reported. multiplier is AFC v1.2.0+; on older
    /// firmware all three stay at -1.
    float multiplier = -1.0f;
    float multiplier_high = -1.0f;
    float multiplier_low = -1.0f;

    /// Seconds AFC waits after a buffer switch before declaring a fault.
    /// -1 = not reported (fault detection off, or older firmware).
    float fault_timer = -1.0f;

    /// Compute fault threshold from error_sensitivity: (11 - sensitivity) * 10 mm
    /// Returns 60mm fallback when sensitivity is 0 (not reported)
    /// Clamps sensitivity to 10 max to ensure threshold >= 10mm
    float fault_threshold() const {
        if (error_sensitivity <= 0.0f)
            return 60.0f;
        float clamped = std::min(error_sensitivity, 10.0f);
        return (11.0f - clamped) * 10.0f;
    }

    /// Map distance_to_fault to danger percentage (0=safe, 100=fault imminent)
    /// Negative or above-threshold distances are treated as safe (0)
    int danger_value() const {
        float max_dist = fault_threshold();
        if (distance_to_fault < 0 || distance_to_fault > max_dist)
            return 0;
        int v = 100 - static_cast<int>((distance_to_fault / max_dist) * 100.0f);
        return std::clamp(v, 0, 100);
    }

    /// Whether the danger level warrants a warning indicator
    bool is_warning() const {
        return danger_value() > 75;
    }
};

/**
 * @brief Information about a single slot/lane
 *
 * This represents one filament slot in an AMS unit.
 * Happy Hare calls these "gates" internally, AFC calls them "lanes".
 */
struct SlotInfo {
    int slot_index = -1;   ///< Slot/lane number (0-based within unit)
    int global_index = -1; ///< Global index across all units
    SlotStatus status = SlotStatus::UNKNOWN;

    // Filament information
    std::string color_name;                      ///< Named color (e.g., "Red", "Blue")
    uint32_t color_rgb = AMS_DEFAULT_SLOT_COLOR; ///< RGB color for UI (0xRRGGBB)
    std::string multi_color_hexes;               ///< Comma-separated hex codes for multi-color
                                                 ///< (e.g., "#D4AF37,#C0C0C0,#B87333")
    std::string material;                        ///< Material type (e.g., "PLA", "PETG", "ABS")
    std::string brand;                           ///< Brand name (e.g., "Polymaker", "eSUN")

    // Catalog product identity — WHICH branded product, not just its material.
    //
    // `material` collapses every PLA product a vendor sells into one string, so
    // brand + material cannot distinguish SUNLU "PLA+ 2.0" from SUNLU "PLA
    // Marble". Both are stored because they answer different questions and can
    // outlive each other:
    //   - catalog_id resolves through FilamentCatalog::resolve_id() to seed the
    //     editor's product list back to the exact row the user picked.
    //   - product_name is the display string, and survives a catalog_id that no
    //     longer resolves (a custom overlay product the user deleted, a bundled
    //     id retired by an app update). Without it a dead id would leave the
    //     lane with no record of what was chosen at all.
    // Both empty = no catalog pick; the editor falls back to preselect-first.
    //
    // Deliberately NOT folded into spool_name: that carries a Spoolman /
    // firmware-label meaning, is written behind our back by AFC/CFS/Snapmaker,
    // is wiped by clear_spoolman_link(), and Snapmaker round-trips it to
    // firmware as SUB_TYPE.
    std::string catalog_id;   ///< assets/filaments.json product id ("sunlu-pla-plus-2-0")
    std::string product_name; ///< Catalog display name ("PLA+ 2.0")

    // Temperature recommendations (from Spoolman or manual entry)
    int nozzle_temp_min = 0; ///< Minimum nozzle temp (°C)
    int nozzle_temp_max = 0; ///< Maximum nozzle temp (°C)
    int bed_temp = 0;        ///< Recommended bed temp (°C)

    // Tool mapping
    int mapped_tool = -1;               ///< Which tool this slot maps to (-1=none)
    bool tool_mapping_override = false; ///< True if user manually remapped this slot's tool
    std::string
        extruder_name; ///< Physical extruder name (e.g., "extruder2") for shared-extruder dedup

    // Spoolman integration
    int spoolman_id = 0;           ///< Spoolman spool ID (0=not tracked)
    int spoolman_filament_id = 0;  ///< Spoolman filament definition ID (0=unknown)
    int spoolman_vendor_id = 0;    ///< Spoolman vendor ID (0=unknown)
    std::string spool_name;        ///< Spool name from Spoolman
    float remaining_weight_g = -1; ///< Remaining filament weight in grams (-1=unknown)
    float total_weight_g = -1;     ///< Total spool weight in grams (-1=unknown)

    // Endless spool support (Happy Hare)
    int endless_spool_group = -1; ///< Endless spool group (-1=not grouped)

    // Error state
    std::optional<SlotError> error; ///< Per-slot error state (nullopt = no error)

    // Length-based remaining filament (CFS measuring wheel, etc.)
    float remaining_length_m = 0.0f; ///< Remaining filament in meters (0 = unknown)

    // Per-slot environment sensors (optional — most backends don't have these)
    std::optional<EnvironmentData> environment; ///< nullopt = no per-slot sensors

    /**
     * @brief Get remaining percentage
     * @return 0-100 or -1 if unknown
     */
    [[nodiscard]] float get_remaining_percent() const {
        if (remaining_weight_g < 0 || total_weight_g <= 0)
            return -1;
        return (remaining_weight_g / total_weight_g) * 100.0f;
    }

    /**
     * @brief Check if this slot has filament data configured
     * @return true if material or custom color is set
     *
     * catalog_id / product_name are deliberately NOT tested here. A catalog
     * pick always writes `material` alongside them (the product's type is the
     * material), so an extra clause could never change the answer — it would
     * only imply a state that cannot occur. Same reasoning applies to the two
     * ghost-slot predicates that mirror this one (ui_ams_slot.cpp,
     * ui_ams_mini_status.cpp); keep all three in agreement.
     */
    [[nodiscard]] bool has_filament_info() const {
        return !material.empty() || color_rgb != AMS_DEFAULT_SLOT_COLOR;
    }

    /**
     * @brief Check if this is a multi-color filament
     * @return true if multi_color_hexes contains color data
     */
    [[nodiscard]] bool is_multi_color() const {
        return !multi_color_hexes.empty();
    }

    /**
     * @brief Check if filament is present in this slot
     * @return true for AVAILABLE, LOADED, FROM_BUFFER, BLOCKED; false for EMPTY, UNKNOWN
     */
    [[nodiscard]] bool is_present() const {
        return status != SlotStatus::EMPTY && status != SlotStatus::UNKNOWN;
    }

    /**
     * @brief Drop every Spoolman handle from this slot
     *
     * Unlink used to zero only spoolman_id, leaving spoolman_filament_id and
     * spoolman_vendor_id behind. Those stale handles then fed
     * SpoolmanSlotSaver's repoint comparison against a filament belonging to a
     * spool this lane is no longer linked to.
     *
     * Locally-editable identity (brand / material / colour / weights) is
     * deliberately KEPT: unlinking means "stop tracking this in Spoolman", not
     * "forget what is in the lane". catalog_id / product_name fall on the KEPT
     * side for the same reason and one stronger: the catalog pick comes from
     * assets/filaments.json, which has no Spoolman record behind it at all —
     * there is no handle here to go stale. Clearing it would drop the user back
     * to the alphabetically-first variant of their material on the next open,
     * which is precisely the failure these fields exist to prevent.
     */
    void clear_spoolman_link() {
        spoolman_id = 0;
        spoolman_filament_id = 0;
        spoolman_vendor_id = 0;
        spool_name.clear();
    }

    /**
     * @brief Fill-bar level for the slot UI, or nullopt to leave the bar as-is.
     *
     * EMPTY/UNKNOWN lanes render empty (0.0) — even when a Spoolman link and
     * material were deliberately RETAINED across an eject (#1071), so a ghost
     * lane never shows a phantom fill (#1071 BUG-1). Present lanes use the real
     * remaining/total ratio when both weights are known; else fall back to 50%
     * when any filament metadata is present (some backends, e.g. Snapmaker RFID,
     * report a total but never a remaining) — an indeterminate half-bar, not a
     * misleadingly-full one; else nullopt (leave unchanged).
     */
    [[nodiscard]] std::optional<float> display_fill_level() const {
        if (!is_present()) {
            return 0.0f;
        }
        if (total_weight_g > 0.0f && remaining_weight_g >= 0.0f) {
            return remaining_weight_g / total_weight_g;
        }
        if (has_filament_info()) {
            return 0.50f;
        }
        return std::nullopt;
    }

    /**
     * @brief Canonical fill-level as an integer percent for subject transport.
     *
     * Encodes display_fill_level() into the 0-100 int convention used by the
     * per-slot fill subjects (AmsState::get_slot_fill_subject): -1 when
     * display_fill_level() is nullopt (no data — leave the render untouched),
     * otherwise the ratio rounded to 0-100 (absent lane -> 0, metadata-only
     * fallback -> 50, real ratio -> lround(ratio*100)). Implemented in terms of
     * display_fill_level() so the two never drift.
     */
    [[nodiscard]] int display_fill_pct() const {
        auto lvl = display_fill_level();
        if (!lvl) {
            return -1;
        }
        return static_cast<int>(std::lround(*lvl * 100.0f));
    }
};

/**
 * @brief Information about an AMS unit
 *
 * Supports multi-unit configurations (e.g., 2x Box Turtles = 16 slots).
 * Most setups have a single unit with 4-8 slots.
 */
struct AmsUnit {
    int unit_index = 0;              ///< Unit number (0-based)
    std::string name;                ///< Internal name for matching (e.g., "Box_Turtle Turtle_1")
    std::string display_name;        ///< Pretty name for UI (e.g., "Turtle 1") — empty = use name
    int slot_count = 0;              ///< Number of slots on this unit
    int first_slot_global_index = 0; ///< Global index of first slot

    std::vector<SlotInfo> slots; ///< Slot information

    // Unit-level status
    bool connected = false;                     ///< Unit communication status
    std::string firmware_version;               ///< Firmware version if available
    std::string serial_number;                  ///< Hardware serial number
    std::optional<EnvironmentData> environment; ///< Per-unit temp/humidity (nullopt = no sensors)

    // Sensors (Happy Hare)
    bool has_encoder = false;         ///< Has filament encoder
    bool has_toolhead_sensor = false; ///< Has toolhead filament sensor
    bool has_slot_sensors = false;    ///< Has per-slot sensors

    // Hub/combiner sensor (AFC Box Turtle, Night Owl, etc.)
    bool has_hub_sensor = false;       ///< Unit has a hub/combiner sensor
    bool hub_sensor_triggered = false; ///< Filament detected at this unit's hub

    // Buffer health (AFC TurtleNeck — one buffer per unit, sits between hub and toolhead)
    std::optional<BufferHealth> buffer_health; ///< Buffer fault state (nullopt = no buffer data)

    // Per-unit topology (for mixed-topology setups like Box Turtle + OpenAMS)
    PathTopology topology = PathTopology::HUB; ///< Filament path topology for this unit

    /// Physical tool label for HUB units (e.g., 4 for extruder4/T4, 5 for extruder5/T5).
    /// -1 means "use min(mapped_tool) from lanes" (the default/PARALLEL behavior).
    int hub_tool_label = -1;

    /// Per-lane hub routing flag. true = lane routes through hub, false = direct to extruder.
    /// Empty vector means routing info unavailable (treat as uniform topology).
    std::vector<bool> lane_is_hub_routed;

    /**
     * @brief Check if any slot in this unit has an error
     * @return true if at least one slot has error.has_value()
     */
    [[nodiscard]] bool has_any_error() const {
        return std::any_of(slots.begin(), slots.end(),
                           [](const SlotInfo& s) { return s.error.has_value(); });
    }

    /**
     * @brief Get slot by local index (within this unit)
     * @param local_index Index within this unit (0 to slot_count-1)
     * @return Pointer to slot info or nullptr if out of range
     */
    [[nodiscard]] const SlotInfo* get_slot(int local_index) const {
        if (local_index < 0 || local_index >= static_cast<int>(slots.size())) {
            return nullptr;
        }
        return &slots[local_index];
    }

    /**
     * @brief Get mutable slot by local index (within this unit)
     * @param local_index Index within this unit (0 to slot_count-1)
     * @return Pointer to slot info or nullptr if out of range
     */
    [[nodiscard]] SlotInfo* get_slot(int local_index) {
        if (local_index < 0 || local_index >= static_cast<int>(slots.size())) {
            return nullptr;
        }
        return &slots[local_index];
    }
};

// ============================================================================
// Spoolman Integration Mode (Happy Hare v4)
// ============================================================================

/**
 * @brief Spoolman integration mode for Happy Hare v4
 *
 * Happy Hare v4 supports 4 Spoolman integration modes:
 * - OFF: No Spoolman integration
 * - READONLY: Reads spool data but doesn't update
 * - PUSH: Pushes gate changes to Spoolman (sets active spool)
 * - PULL: Pulls spool assignments from Spoolman (Spoolman is source of truth)
 */
enum class SpoolmanMode { OFF = 0, READONLY = 1, PUSH = 2, PULL = 3 };

/**
 * @brief Convert SpoolmanMode to display string
 */
inline const char* spoolman_mode_to_string(SpoolmanMode mode) {
    switch (mode) {
    case SpoolmanMode::OFF:
        return "Off";
    case SpoolmanMode::READONLY:
        return "Read Only";
    case SpoolmanMode::PUSH:
        return "Push";
    case SpoolmanMode::PULL:
        return "Pull";
    default:
        return "Unknown";
    }
}

/**
 * @brief Parse SpoolmanMode from Happy Hare string value
 * @param str String from printer.mmu.spoolman_support
 * @return Corresponding SpoolmanMode
 */
inline SpoolmanMode spoolman_mode_from_string(std::string_view str) {
    if (str == "off" || str == "Off")
        return SpoolmanMode::OFF;
    if (str == "readonly" || str == "Read Only")
        return SpoolmanMode::READONLY;
    if (str == "push" || str == "Push")
        return SpoolmanMode::PUSH;
    if (str == "pull" || str == "Pull")
        return SpoolmanMode::PULL;
    return SpoolmanMode::OFF;
}

/// Encoder-based clog detection (Happy Hare mmu.encoder.*)
struct EncoderClogInfo {
    bool enabled = false;
    int flow_rate = -1;         // 0-100% (-1=unavailable)
    int detection_mode = 0;     // 0=unknown, 1=manual, 2=auto
    float desired_headroom = 0; // target headroom (mm)
    float detection_length = 0; // total detection distance (mm)
    float headroom = 0;         // current headroom (mm)
    float min_headroom = 0;     // minimum headroom reached (mm)

    /// Get clog percentage: 0=full headroom, 100=clogged
    [[nodiscard]] int get_clog_pct() const {
        if (detection_length <= 0)
            return 0;
        float used = detection_length - headroom;
        int pct = static_cast<int>((used / detection_length) * 100.0f + 0.5f);
        return std::clamp(pct, 0, 100);
    }

    /// Warning: min_headroom has dipped below desired_headroom
    [[nodiscard]] bool is_warning() const {
        return desired_headroom > 0 && min_headroom < desired_headroom;
    }
};

/// Flowguard clog/tangle detection (Happy Hare mmu.flowguard.*)
struct FlowguardInfo {
    bool enabled = false;
    bool active = false;
    std::string trigger; // "CLOG", "TANGLE", or ""
    float level = 0;     // -1.0 (tangle) to +1.0 (clog)
    float max_clog = 0;
    float max_tangle = 0; // negative value
};

/**
 * @brief Complete AMS system state
 *
 * This is the top-level structure containing all AMS information.
 */
struct AmsSystemInfo {
    AmsType type = AmsType::NONE;
    std::string type_name; ///< "Happy Hare", "AFC", etc.
    std::string version;   ///< System version string

    // Current state
    int current_tool = -1; ///< Active tool (-1=none, -2=bypass for HH)
    int current_slot = -1; ///< Active slot (-1=none, -2=bypass for HH)

    /// Whether a toolhead is on the carriage. UNKNOWN on machines without one,
    /// which is every backend except the toolchanger-capable ones — they are
    /// unaffected by this field. See MountState (#1229).
    MountState mount_state = MountState::UNKNOWN;
    /// Tool number on the carriage when mount_state == MOUNTED, else -1.
    int mounted_tool = -1;

    int pending_target_slot = -1;  ///< Target slot during tool change (-1=none)
    int current_toolchange = -1;   ///< Current tool change number (-1=none yet, 0-based)
    int number_of_toolchanges = 0; ///< Total expected tool changes this print
    bool filament_loaded = false;  ///< Filament at extruder

    /// Slot the firmware has pre-staged for the NEXT toolchange (-1 = none).
    /// AFC publishes this as `AFC.next_lane` during a multicolor print; resolved
    /// to a slot index here. Distinct from pending_target_slot, which is the
    /// destination of the toolchange already under way.
    int next_slot = -1;
    bool filament_runout =
        false; ///< CFS: active path empty (box.filament_useup); UI gates display on paused state
    AmsAction action = AmsAction::IDLE; ///< Current operation
    std::string operation_detail;       ///< Detailed operation string

    /// Granular firmware sub-phase of the active load/unload, for backends that
    /// expose one. Used to drive a multi-step progress bar that mirrors the real
    /// firmware sequence instead of the coarse AmsAction. -1 = none /
    /// not-applicable. The index meaning is backend-specific and matches that
    /// backend's get_operation_step_model() phase_id values:
    ///   - Snapmaker U1 (4 steps): 0 = Home, 1 = Select, 2 = Heat,
    ///     3 = Move (Retract on unload / Feed on load).
    ///   - AD5X IFS (3 synthesized steps): 0 = Heat,
    ///     1 = Cut (unload) / Feed (load), 2 = Retract (unload) / Purge (load).
    /// Default -1.
    int operation_phase = -1;

    /// True when an operation is active but no phase-progress signal (temp-value
    /// change, head transition, motion, or phase change) has arrived within the
    /// backend's indeterminate threshold (~8s). On constrained backends (AD5X
    /// IFS) the shared main-thread status feed can starve while klippy runs a
    /// blocking load/unload macro, freezing the live "Heat 225/230" number so it
    /// reads as a hang. The UI swaps the frozen number for an indeterminate
    /// "Working…" busy state while this is true (#1065 row 14). Default false.
    bool operation_indeterminate = false;

    // Units
    std::vector<AmsUnit> units; ///< All AMS units
    int total_slots = 0;        ///< Sum of all slots across units

    // Capability flags
    bool supports_endless_spool = false;
    bool supports_tool_mapping = false;
    bool supports_bypass = false;            ///< Has bypass selector position
    bool has_hardware_bypass_sensor = false; ///< true=auto-detect sensor, false=virtual/manual
    TipMethod tip_method = TipMethod::CUT;   ///< How filament tip is handled during unload
    bool supports_purge = false;             ///< Has purge capability after load

    // Active alerts (persistent error conditions from hardware)
    std::vector<AmsAlert> alerts; ///< Current system/unit/slot alerts

    // Happy Hare v4 extended status fields
    SpoolmanMode spoolman_mode = SpoolmanMode::OFF; ///< Spoolman integration mode
    int pending_spool_id = -1;                      ///< Pending spool assignment (v4)

    /// Spoolman base URL the firmware itself is configured against, when it
    /// publishes one (AFC `AFC.spoolman`). Empty = not reported / not configured.
    /// Informational: HelixScreen resolves Spoolman through Moonraker, not this.
    std::string spoolman_url;

    /// Firmware has a saved toolhead position it can restore (AFC
    /// `AFC.position_saved`). Set while an error interrupted a print mid-move.
    bool position_saved = false;

    std::string espooler_state;      ///< eSpooler state: "rewind"/"assist"/""
    std::string sync_feedback_state; ///< Sync feedback: "compressed"/"tension"/"neutral"/"disabled"
    bool sync_drive = false;         ///< Gear synced to extruder motor
    int clog_detection = 0;          ///< Clog detection: 0=off, 1=manual, 2=auto
    int encoder_flow_rate = -1;      ///< Encoder flow rate (-1=unavailable)
    EncoderClogInfo encoder_info;    ///< Encoder-based clog detection state
    FlowguardInfo flowguard_info;    ///< Flowguard clog/tangle detection state
    float sync_feedback_flow_rate = -1; ///< Sync feedback flow rate
    float sync_feedback_bias = -2;      ///< Modelled bias [-1.0,1.0], -2=unavailable
    float sync_feedback_bias_raw = -2;  ///< Raw sensor bias [-1.0,1.0], -2=unavailable
    float toolchange_purge_volume = 0;  ///< Slicer purge volume for toolchanges

    // Tool-to-slot mapping (Happy Hare uses "gate" internally)
    std::vector<int> tool_to_slot_map; ///< tool_to_slot_map[tool] = slot

    /**
     * @brief Get slot by global index (across all units)
     * @param global_index Global slot index (0 to total_slots-1)
     * @return Pointer to slot info or nullptr if out of range
     */
    [[nodiscard]] const SlotInfo* get_slot_global(int global_index) const {
        for (const auto& unit : units) {
            if (global_index >= unit.first_slot_global_index &&
                global_index < unit.first_slot_global_index + unit.slot_count) {
                int local_idx = global_index - unit.first_slot_global_index;
                return unit.get_slot(local_idx);
            }
        }
        return nullptr;
    }

    /**
     * @brief Get mutable slot by global index (across all units)
     * @param global_index Global slot index (0 to total_slots-1)
     * @return Pointer to slot info or nullptr if out of range
     */
    [[nodiscard]] SlotInfo* get_slot_global(int global_index) {
        for (auto& unit : units) {
            if (global_index >= unit.first_slot_global_index &&
                global_index < unit.first_slot_global_index + unit.slot_count) {
                int local_idx = global_index - unit.first_slot_global_index;
                return unit.get_slot(local_idx);
            }
        }
        return nullptr;
    }

    /**
     * @brief Get the currently active slot info
     * @return Pointer to active slot or nullptr if none selected
     */
    [[nodiscard]] const SlotInfo* get_active_slot() const {
        if (current_slot < 0)
            return nullptr;
        return get_slot_global(current_slot);
    }

    /**
     * @brief Check if system is available and connected
     * @return true if AMS type is detected and has at least one unit
     */
    [[nodiscard]] bool is_available() const {
        return type != AmsType::NONE && !units.empty();
    }

    /**
     * @brief Check if an operation is in progress
     * @return true if actively loading, unloading, etc.
     */
    [[nodiscard]] bool is_busy() const {
        return action != AmsAction::IDLE && action != AmsAction::ERROR;
    }

    // === Multi-unit helpers ===

    /**
     * @brief Check if this is a multi-unit setup (2+ physical units)
     * @return true if more than one AmsUnit exists
     */
    [[nodiscard]] bool is_multi_unit() const {
        return units.size() > 1;
    }

    /**
     * @brief Get number of physical units
     * @return Number of AmsUnit entries
     */
    [[nodiscard]] int unit_count() const {
        return static_cast<int>(units.size());
    }

    /**
     * @brief Position in `units` of the unit that contains a global slot index
     *
     * Deliberately the POSITION, not AmsUnit::unit_index. Backends index
     * get_unit_topology() by position — AFC sorts `units` alphabetically and
     * matches by name inside that accessor — and several backends never assign
     * unit_index at all, leaving it at its 0 default. Every existing caller
     * (ams_drawing_utils, ui_panel_ams_overview) already passes a loop position.
     *
     * @param global_index Global slot index (0 to total_slots-1)
     * @return Index into `units`, or -1 if no unit covers the slot
     */
    [[nodiscard]] int get_unit_position_for_slot(int global_index) const {
        for (size_t i = 0; i < units.size(); ++i) {
            const AmsUnit& unit = units[i];
            if (global_index >= unit.first_slot_global_index &&
                global_index < unit.first_slot_global_index + unit.slot_count) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    /**
     * @brief Get the unit that contains a given global slot index
     * @param global_index Global slot index (0 to total_slots-1)
     * @return Pointer to containing AmsUnit or nullptr if out of range
     */
    [[nodiscard]] const AmsUnit* get_unit_for_slot(int global_index) const {
        const int pos = get_unit_position_for_slot(global_index);
        return pos < 0 ? nullptr : &units[static_cast<size_t>(pos)];
    }

    /**
     * @brief Get mutable unit that contains a given global slot index
     * @param global_index Global slot index (0 to total_slots-1)
     * @return Pointer to containing AmsUnit or nullptr if out of range
     */
    [[nodiscard]] AmsUnit* get_unit_for_slot(int global_index) {
        const int pos = get_unit_position_for_slot(global_index);
        return pos < 0 ? nullptr : &units[static_cast<size_t>(pos)];
    }

    /**
     * @brief Get unit by index
     * @param unit_index Unit index (0 to unit_count()-1)
     * @return Pointer to AmsUnit or nullptr if out of range
     */
    [[nodiscard]] const AmsUnit* get_unit(int unit_index) const {
        if (unit_index < 0 || unit_index >= static_cast<int>(units.size())) {
            return nullptr;
        }
        return &units[unit_index];
    }

    /**
     * @brief Get the unit index that contains the currently active slot
     * @return Unit index (0-based) or -1 if no active slot
     */
    [[nodiscard]] int get_active_unit_index() const {
        if (current_slot < 0)
            return -1;
        const auto* unit = get_unit_for_slot(current_slot);
        if (!unit)
            return -1;
        return unit->unit_index;
    }
};

namespace helix {

/**
 * @brief Decide whether enabling bypass must be preceded by an implicit unload.
 *
 * The AMS sidebar's bypass toggle used to unload whenever a slot was loaded,
 * then enable bypass once the unload finished. On backends whose users have a
 * console (AFC, Happy Hare) that ejects filament the user never asked to eject,
 * so those backends report @p allows_implicit_chaining false and the UI sends
 * only the bypass command, letting the firmware refuse it if it wants to
 * (prestonbrown/helixscreen#1229). Backends with no console fallback keep the
 * chaining.
 *
 * Pure so the policy is testable without LVGL; the sidebar calls this rather
 * than restating the condition.
 *
 * @param info Current AMS system state
 * @param allows_implicit_chaining AmsBackend::allows_implicit_chaining() for the active backend
 * @return true if the UI should unload the active slot before enabling bypass
 */
[[nodiscard]] inline bool should_unload_before_bypass(const AmsSystemInfo& info,
                                                      bool allows_implicit_chaining) {
    return allows_implicit_chaining && info.current_slot >= 0 && info.filament_loaded;
}

/**
 * @brief Tool number implied by a Klipper extruder name: "extruder" = 0, "extruderN" = N.
 *
 * A positional convention, and the third of three numbering systems that can all
 * disagree on one machine: Klipper's `tool T<n>` objects, AFC's per-lane `map`
 * aliases, and extruder-name position. On the reporter's toolchanger AFC maps T0
 * to the `extruder5` lane while Klipper's `tool T0` is `extruder`
 * (prestonbrown/helixscreen#1229).
 *
 * Returns nullopt rather than a fallback for anything that is not an
 * `extruder`-prefixed name, so callers must decide what an unidentifiable
 * extruder means. Badge rendering needs that distinction: silently mapping an
 * empty name to 0 would label every toolhead "E0" on backends that never
 * populate SlotInfo::extruder_name at all.
 *
 * @param ext_name Klipper extruder object name, e.g. "extruder" or "extruder5"
 * @return Tool number, or nullopt if @p ext_name is not a valid extruder name
 */
[[nodiscard]] inline std::optional<int> tool_number_for_extruder(std::string_view ext_name) {
    constexpr std::string_view kPrefix = "extruder";
    if (ext_name == kPrefix) {
        return 0;
    }
    if (ext_name.size() <= kPrefix.size() || ext_name.substr(0, kPrefix.size()) != kPrefix) {
        return std::nullopt;
    }
    const std::string_view digits = ext_name.substr(kPrefix.size());
    // No real machine has a four-digit extruder index; the bound also keeps the
    // accumulate below well clear of overflow without pulling in <climits>.
    if (digits.size() > 3 || !std::all_of(digits.begin(), digits.end(),
                                          [](unsigned char c) { return std::isdigit(c) != 0; })) {
        return std::nullopt;
    }
    int value = 0;
    for (const char c : digits) {
        value = value * 10 + (c - '0');
    }
    return value;
}

} // namespace helix

/**
 * @brief Filament requirement from G-code analysis
 *
 * Used for print preview to show which colors are needed.
 */
struct FilamentRequirement {
    int tool_index = -1;                         ///< Tool number from G-code (T0, T1, etc.)
    uint32_t color_rgb = AMS_DEFAULT_SLOT_COLOR; ///< Color hint from slicer
    std::string material;                        ///< Material hint from slicer (if available)
    int mapped_slot = -1;                        ///< Which slot is mapped to this tool

    /**
     * @brief Check if this requirement is satisfied by a slot
     * @return true if a slot is mapped to this tool
     */
    [[nodiscard]] bool is_satisfied() const {
        return mapped_slot >= 0;
    }
};

/**
 * @brief Print color requirements summary
 */
struct PrintColorInfo {
    std::vector<FilamentRequirement> requirements;
    int initial_tool = 0;       ///< First tool used in print
    bool all_satisfied = false; ///< All requirements have mapped slots
};

// ============================================================================
// Dryer Types (for AMS systems with integrated drying)
// ============================================================================

/**
 * @brief Preset drying profile
 *
 * Standard drying profiles for common filament materials.
 * Can be overridden via settings.json "dryer_presets" array.
 */
struct DryingPreset {
    std::string name;       ///< Preset name (e.g., "PLA", "PETG", "ABS")
    float temp_c = 45.0f;   ///< Target temperature in Celsius
    int duration_min = 240; ///< Drying duration in minutes
    int fan_pct = 50;       ///< Fan speed percentage (0-100)

    /**
     * @brief Create a drying preset
     * @param n Preset name
     * @param t Temperature in Celsius
     * @param d Duration in minutes
     * @param f Fan speed percentage
     */
    DryingPreset(std::string n, float t, int d, int f = 50)
        : name(std::move(n)), temp_c(t), duration_min(d), fan_pct(f) {}
    DryingPreset() = default;
};

/**
 * @brief Dryer capability and state information
 *
 * Not all AMS systems have integrated dryers. Currently only ACE Pro
 * has dryer support. This struct provides a generic interface that other
 * backends can implement when dryer hardware becomes available.
 */
struct DryerInfo {
    bool supported = false;           ///< Does this AMS have a dryer?
    bool active = false;              ///< Currently drying?
    bool allows_during_print = false; ///< Can run while printing? (backend capability)

    // Current state
    float current_temp_c = 0.0f; ///< Current chamber temperature
    float target_temp_c = 0.0f;  ///< Target temperature (0 = off)
    int duration_min = 0;        ///< Total drying duration set
    int remaining_min = 0;       ///< Minutes remaining
    int fan_pct = 0;             ///< Current fan speed (0-100)

    // Hardware capabilities
    float min_temp_c = 35.0f;          ///< Minimum settable temperature
    float max_temp_c = 70.0f;          ///< Maximum settable temperature
    int max_duration_min = 720;        ///< Maximum drying time (12h default)
    bool supports_fan_control = false; ///< Can fan speed be set independently?

    /**
     * @brief Get progress as percentage
     * @return 0-100 percentage, or -1 if not drying
     */
    [[nodiscard]] int get_progress_pct() const {
        if (!active || duration_min <= 0)
            return -1;
        int elapsed = duration_min - remaining_min;
        // Clamp to valid range (handles firmware reporting remaining > duration)
        if (elapsed < 0)
            elapsed = 0;
        if (elapsed > duration_min)
            elapsed = duration_min;
        return (elapsed * 100) / duration_min;
    }

    /**
     * @brief Check if dryer is at target temperature
     * @param tolerance_c Temperature tolerance in Celsius (default 2°C)
     * @return true if within tolerance of target
     */
    [[nodiscard]] bool is_at_temp(float tolerance_c = 2.0f) const {
        if (target_temp_c <= 0)
            return false;
        return std::abs(current_temp_c - target_temp_c) <= tolerance_c;
    }
};

/**
 * @brief Get default drying presets
 *
 * Returns presets derived from the filament database, one per compatibility group.
 * Uses filament::get_drying_presets_by_group() as the single source of truth.
 * These can be overridden via settings.json "dryer_presets" array.
 *
 * @return Vector of default DryingPreset structs
 */
inline std::vector<DryingPreset> get_default_drying_presets() {
    constexpr int DEFAULT_FAN_PCT = 50;

    std::vector<DryingPreset> result;
    for (const auto& fp : filament::get_drying_presets_by_group()) {
        result.emplace_back(fp.name, static_cast<float>(fp.temp_c), fp.time_min, DEFAULT_FAN_PCT);
    }
    return result;
}

// ============================================================================
// Endless Spool Types
// ============================================================================

namespace helix::printer {

/**
 * @brief Capabilities for endless spool feature
 *
 * Describes whether endless spool is supported and whether the UI can modify
 * the configuration. Different backends have different capabilities:
 * - AFC: Fully editable, per-slot backup configuration
 * - Happy Hare: Read-only, group-based (configured via mmu_vars.cfg)
 * - Mock: Configurable for testing both modes
 */
struct EndlessSpoolCapabilities {
    bool supported = false; ///< Does backend support endless spool?
    bool editable = false;  ///< Can UI modify configuration?
    std::string
        description; ///< Human-readable description (e.g., "Per-slot backup", "Group-based")
};

/**
 * @brief Configuration for a single slot's endless spool backup
 *
 * Represents which slot will be used as a backup when the primary slot runs out.
 * This provides a unified view regardless of backend (AFC's runout_lane or
 * Happy Hare's endless_spool_groups).
 */
struct EndlessSpoolConfig {
    int slot_index = 0;   ///< Slot this config applies to
    int backup_slot = -1; ///< Backup slot index (-1 = no backup)
};

/**
 * @brief Capabilities for tool mapping feature
 *
 * Describes whether tool mapping is supported and whether the UI can modify
 * the configuration. Different backends have different capabilities:
 * - AFC: Fully editable, per-lane tool assignment via SET_MAP
 * - Happy Hare: Fully editable, tool-to-gate mapping via MMU_TTG_MAP
 * - Mock: Configurable for testing both modes
 * - ACE: Not supported (1:1 fixed mapping)
 * - ToolChanger: Not supported (tools ARE slots)
 */
struct ToolMappingCapabilities {
    bool supported = false;  ///< Does this backend support tool mapping?
    bool editable = false;   ///< Can the UI modify the mapping?
    std::string description; ///< UI hint text (e.g., "Per-lane tool assignment via SET_MAP")
};

/**
 * @brief Action type for dynamic device controls
 */
enum class ActionType {
    BUTTON,   ///< Simple action button
    TOGGLE,   ///< On/off toggle switch
    SLIDER,   ///< Value slider with min/max
    DROPDOWN, ///< Selection from options list
    INFO      ///< Read-only information display
};

/**
 * @brief Convert ActionType to string for display/debug
 */
inline const char* action_type_to_string(ActionType type) {
    switch (type) {
    case ActionType::BUTTON:
        return "Button";
    case ActionType::TOGGLE:
        return "Toggle";
    case ActionType::SLIDER:
        return "Slider";
    case ActionType::DROPDOWN:
        return "Dropdown";
    case ActionType::INFO:
        return "Info";
    default:
        return "Unknown";
    }
}

/**
 * @brief Section metadata for UI rendering
 *
 * Groups related device actions together in the UI.
 */
struct DeviceSection {
    std::string id;          ///< Section identifier (e.g., "calibration")
    std::string label;       ///< Display label (e.g., "Calibration")
    int display_order;       ///< Sort order (0 = first)
    std::string description; ///< Short description for settings row
};

/**
 * @brief Represents a single device-specific action
 *
 * Backends populate these to expose unique features without hardcoding in UI.
 */
struct DeviceAction {
    std::string id;                   ///< Unique action ID (e.g., "afc_calibration")
    std::string label;                ///< Display label
    std::string icon;                 ///< Icon name
    std::string section;              ///< Section ID this action belongs to
    std::string description;          ///< Optional tooltip/hint text
    ActionType type;                  ///< Control type
    std::any current_value;           ///< Current value (for toggles/sliders/dropdowns)
    std::vector<std::string> options; ///< Options for dropdown type
    float min_value = 0;              ///< Min value for slider type
    float max_value = 100;            ///< Max value for slider type
    std::string unit;                 ///< Display unit (e.g., "mm", "%")
    int slot_index = -1;              ///< If action is per-slot (-1 = system-wide)
    bool enabled = true;              ///< Whether action is currently available
    std::string disable_reason;       ///< Why disabled (if applicable)
};

} // namespace helix::printer
