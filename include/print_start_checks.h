// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
/**
 * @file print_start_checks.h
 * @brief Pure core of the print-start gate pipeline.
 *
 * Gates are pure functions of a PrintStartContext snapshot; the controller
 * gathers the snapshot (gather_print_start_context()) on every run_gates_from()
 * call so each resume re-reads live state. No LVGL objects or subjects are
 * touched here. Design: docs/superpowers/specs/2026-08-17-print-start-gate-pipeline-design.md
 */

#include "ams_types.h"
#include "filament_mapper.h"
#include "moonraker_types.h"

#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace helix {

/// Severity for a gate dialog. Maps 1:1 onto ModalSeverity in the controller
/// (kept separate so this header stays LVGL-free).
enum class GateSeverity { Info, Warning, Error };

/// Everything a gate may read. Gathered fresh by the controller per pipeline
/// (re-)entry; gates never fetch singleton state themselves.
struct PrintStartContext {
    // ---- file (from PrintSelectDetailView; valid only when has_detail_view) ----
    bool has_detail_view = false;
    std::optional<FileMetadata> metadata;
    std::vector<ToolMapping> mappings;
    std::vector<GcodeToolInfo> tool_info;
    std::vector<AvailableSlot> available_slots;
    std::vector<std::string> filament_materials;
    std::set<int> tools_used;
    std::map<int, int> effective_remap;
    size_t filament_color_count = 0; ///< filament_colors_.size() on the controller

    // ---- environment (from AmsState / FilamentSensorManager) ----
    bool ams_available = false;           ///< AmsState::is_available()
    bool ams_manages_filament = false;    ///< any backend present
    bool has_active_backend = false;      ///< AmsState::get_backend() != nullptr
    bool any_auto_unload_backend = false; ///< any backend->auto_unloads_after_print()
    bool any_bypass_active = false;       ///< AmsState::any_bypass_active()
    /// Per-backend answer to toolhead_filament_unaccounted(), indexed like
    /// AmsState backends. nullopt = backend cannot determine.
    std::vector<std::optional<bool>> toolhead_unaccounted;
    /// Lane-truth result (tool_index, slot_index) for the print's required
    /// lanes; populated only when ams_manages_filament && has_active_backend.
    std::vector<std::pair<int, int>> empty_required_lanes;
    std::optional<SlotInfo> external_spool; ///< AmsState::get_external_spool_info()

    // ---- non-AMS aggregate runout fallback ----
    bool runout_enabled = false;
    bool runout_available = false;
    bool runout_detected = false;
};

struct CheckResult {
    enum class Verdict { Pass, Warn, Block };
    Verdict verdict = Verdict::Pass;
    std::string title;         // already-lv_tr'd by the gate
    std::string body;          // already-lv_tr'd by the gate
    std::string proceed_label; // "Start Anyway" / "Start Print"; empty for Block
    GateSeverity severity = GateSeverity::Warning;
};

struct PrintStartGate {
    std::string_view name;                             // for logging
    CheckResult (*evaluate)(const PrintStartContext&); // pure fn, no captures
};

/// Per-tool material mismatch detail (ported verbatim from
/// PrintStartController::MaterialMismatchDetail).
struct MaterialMismatchDetail {
    int tool_index = 0;
    std::string expected_material;
    std::string loaded_material;
    int expected_nozzle_min = 0;
    int expected_nozzle_max = 0;
    int expected_bed_temp = 0;
    int loaded_nozzle_min = 0;
    int loaded_nozzle_max = 0;
    int loaded_bed_temp = 0;
};

// ---- pure rules (each is the testable half of one gate) ----

/// Tools with no matching AMS slot. Single-color prints need no mapping;
/// bypass feeds without passing any slot (guaranteed noise); no mappings means
/// no AMS to resolve against.
std::vector<int> unresolved_tools_in(const PrintStartContext& ctx);

/// {needed_g, remaining_g} when the assigned external spool cannot cover the
/// print; nullopt otherwise. Weight falls back to a length-based estimate via
/// the spool's material density when the slicer emitted no weight.
std::optional<std::pair<float, float>> insufficient_spool_weight_in(const PrintStartContext& ctx);

/// Material mismatches, AMS path (ToolMapping::material_mismatch) and non-AMS
/// path (gcode material vs external spool), with filament-database temps.
std::vector<MaterialMismatchDetail> material_mismatches_in(const PrintStartContext& ctx);

/// The ordered production gate list. Order is behavior-critical: it preserves
/// the pre-pipeline check order with the two new gates inserted at 2 and 3.
const std::vector<PrintStartGate>& default_print_start_gates();

} // namespace helix
