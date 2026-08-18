// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ams_types.h"

namespace helix::ui {

/**
 * @brief Which of the three dispatch tiers a filament operation should take.
 *
 * FilamentPanel::execute_load() has always been a three-tier router — AMS
 * backend, then the user's configured macro, then a raw-gcode fallback — but
 * the other two dispatch surfaces never learned the tiering. AmsOperationSidebar
 * silently returns when there is no backend; the runout handler navigates the
 * user to the Filament panel for a load and reaches a params-suppressed macro
 * for unload/purge. Same question, three answers.
 */
enum class FilamentTier {
    AmsBackend, ///< Tier 1 — the AMS backend owns the operation
    Macro,      ///< Tier 2 — the configured StandardMacros slot
    RawGcode,   ///< Tier 3 — built-in extrude/retract fallback
    Refused,    ///< Do not dispatch; see FilamentOpPlan::refusal
};

/// Why a plan declined to dispatch. Each maps to different caller-side copy.
enum class FilamentRefusal {
    None,
    SelectSlot,     ///< Load: the backend wants a slot and none resolved
    NothingLoaded,  ///< Unload: the selected slot has no filament to pull
    AlreadyMounted, ///< The requested tool is already on the carriage
};

/// Which backend entry point tier 1 should call. Load is NOT always
/// load_filament(): a seated machine swaps via change_tool(), which is the rule
/// AmsOperationSidebar has always applied and FilamentPanel never has.
enum class AmsCall {
    None,
    Load,       ///< load_filament(arg)
    Unload,     ///< unload_filament(arg)
    ChangeTool, ///< change_tool(arg) — arg is a TOOL number, not a slot
};

struct FilamentOpPlan {
    FilamentTier tier = FilamentTier::Refused;
    FilamentRefusal refusal = FilamentRefusal::None;
    AmsCall ams_call = AmsCall::None;
    int ams_arg = -1; ///< Slot index, or tool number when ams_call == ChangeTool
};

/**
 * @brief The backend answers the planner needs, lifted out of AmsBackend.
 *
 * Taken as plain values rather than an AmsBackend* so the whole decision is
 * testable in a binary with no printer and no display — the same seam
 * test_filament_op_slot_resolver.cpp uses for its slot_loaded predicate.
 */
struct BackendCaps {
    bool present = false;
    bool requires_slot_selection_for_load =
        false; ///< AmsBackend::requires_slot_selection_for_load()
    /// AmsBackend::needs_unload_before_load(info, target_slot) — answered for the
    /// SAME slot the plan targets. It is a per-lane question: on a MIXED unit a
    /// direct-fed lane and a hub-routed one get different answers.
    bool needs_unload_before_load = false;
    bool is_tool_changer = false; ///< get_type() == AmsType::TOOL_CHANGER
    /// AmsBackend::change_tool_completes_load() — answered for the SAME slot the
    /// plan targets, because one backend can answer differently per lane.
    bool change_tool_completes_load = true;
};

/**
 * @brief Plan a Load.
 *
 * Tier 1 is gated on requires_slot_selection_for_load(), NOT on the backend
 * merely existing: its default is `!is_bypass_active()`, so bypass deliberately
 * falls through to the user's LOAD_FILAMENT macro. Preserve that — it is how a
 * bypass spool loads at all.
 *
 * The already-mounted refusal is the fix for debug bundle 9KRXZ62P: SELECT_TOOL
 * on the tool already on the carriage is a firmware no-op, and dispatching it
 * left the Load button spinning for 120 s. AmsOperationSidebar has carried this
 * guard privately since it was written; FilamentPanel and the runout handler
 * never had it.
 *
 * @param sys           Backend system info (current_slot, per-slot mapped_tool).
 * @param caps          Backend capability answers.
 * @param target_slot   Slot the user asked for; < 0 when none resolved.
 * @param macro_available StandardMacros LoadFilament slot is non-empty.
 */
[[nodiscard]] inline FilamentOpPlan plan_load(const AmsSystemInfo& sys, const BackendCaps& caps,
                                              int target_slot, bool macro_available) {
    if (caps.present && caps.requires_slot_selection_for_load) {
        if (target_slot < 0) {
            return {FilamentTier::Refused, FilamentRefusal::SelectSlot, AmsCall::None, -1};
        }
        if (caps.is_tool_changer && sys.current_slot >= 0 && sys.current_slot == target_slot) {
            return {FilamentTier::Refused, FilamentRefusal::AlreadyMounted, AmsCall::None, -1};
        }
        // Load-vs-swap: a machine that already has filament seated cannot simply
        // feed another lane. Centralized in needs_unload_before_load() so the UI
        // and backend agree (#968).
        //
        // An unmapped target slot has no tool number to change to, so it falls
        // through to the plain load below rather than synthesising an unload.
        // There used to be an AmsCall::UnloadActive arm here; it called
        // unload_active_filament() and stopped, so the user got a swap stepper, a
        // pulsing target slot, filament on the floor, and no load. Nothing chained
        // the second half. Falling through is correct on every backend the arm
        // could actually reach:
        //   - ACE: change_tool() is `return load_filament(...)`, so the two arms
        //     were the same command.
        //   - QIDI: load_filament() scans for a LOADED slot and prepends its
        //     unload gcode itself (ams_backend_qidi.cpp).
        //   - AFC: load_filament() emits `CHANGE_TOOL LANE={n}`, which IS AFC's
        //     toolchange verb (unload + load).
        //   - CFS cannot reach it — its slots are mapped 1:1 to tools, so
        //     mapped_tool is never < 0.
        // Happy Hare is the one backend whose load_filament() is a plain
        // `MMU_LOAD GATE={n}`, and it is also the one the UI is forbidden to help:
        // allows_implicit_chaining() is false there precisely so the screen sends
        // one command per user action and lets the firmware refuse (#1229). An
        // unasked-for eject is the harm that rule exists to prevent, and it is
        // exactly what the old arm did.
        // ...and only where that tool change IS the load. Where it is merely the
        // first half (a multiACE ACE bay: `T3` mounts the head, `ACE_LOAD_HEAD
        // HEAD=h ACE=a SLOT=s` feeds it), fall through to the plain load below
        // and let the backend emit the whole sequence -- the same answer QIDI
        // already relies on.
        if (caps.needs_unload_before_load && caps.change_tool_completes_load &&
            sys.current_slot != target_slot) {
            const SlotInfo* slot_info = sys.get_slot_global(target_slot);
            if (slot_info && slot_info->mapped_tool >= 0) {
                return {FilamentTier::AmsBackend, FilamentRefusal::None, AmsCall::ChangeTool,
                        slot_info->mapped_tool};
            }
        }
        return {FilamentTier::AmsBackend, FilamentRefusal::None, AmsCall::Load, target_slot};
    }

    if (macro_available) {
        return {FilamentTier::Macro, FilamentRefusal::None, AmsCall::None, target_slot};
    }
    return {FilamentTier::RawGcode, FilamentRefusal::None, AmsCall::None, target_slot};
}

/**
 * @brief Is there anything at `slot` worth unloading?
 *
 * Answers plan_unload()'s `target_is_loaded`. Shared because the two surfaces
 * answered it differently the moment they were wired separately, which is the
 * divergence this whole file exists to end.
 *
 * The `is_current_slot` arm is not a convenience — it is the recovery case.
 * A runout clears the lane's own sensor while filament is still at the head
 * (#995), and #1199 deliberately keeps Unload reachable there. It also covers
 * backends that ignore the slot entirely (AD5X sends IFS_REMOVE_CURRENT_PRUTOK)
 * and the sidebar's own Unload button, which asks for "whatever is active" and
 * so has no per-slot sensor to consult.
 */
[[nodiscard]] inline bool unload_target_is_loaded(bool slot_actively_loaded,
                                                  bool slot_filament_at_toolhead,
                                                  bool is_current_slot) {
    return slot_actively_loaded || slot_filament_at_toolhead || is_current_slot;
}

/**
 * @brief Plan an Unload.
 *
 * Deliberately asymmetric with plan_load: tier 1 is gated on the backend merely
 * existing, because bypass unload STAYS on the backend — AFC calls the user's
 * unload macro itself when bypass is enabled. Routing bypass unload to tier 2
 * here would run that macro twice.
 *
 * @param target_is_loaded  slot_is_actively_loaded(slot) || slot_has_filament_at_toolhead(slot)
 */
[[nodiscard]] inline FilamentOpPlan plan_unload(const BackendCaps& caps, int target_slot,
                                                bool target_is_loaded, bool macro_available) {
    if (caps.present) {
        if (target_slot < 0 || !target_is_loaded) {
            return {FilamentTier::Refused, FilamentRefusal::NothingLoaded, AmsCall::None, -1};
        }
        return {FilamentTier::AmsBackend, FilamentRefusal::None, AmsCall::Unload, target_slot};
    }

    if (macro_available) {
        return {FilamentTier::Macro, FilamentRefusal::None, AmsCall::None, target_slot};
    }
    return {FilamentTier::RawGcode, FilamentRefusal::None, AmsCall::None, target_slot};
}

} // namespace helix::ui
