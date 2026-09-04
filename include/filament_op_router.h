// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file filament_op_router.h
 * @brief Tiers 2 and 3 of the shared filament dispatch ladder.
 *
 * filament_op_dispatch.h answers *which* tier a Load/Unload takes. This header
 * owns what the two non-backend tiers actually do, so the three dispatch
 * surfaces (FilamentPanel, AmsOperationSidebar, FilamentRunoutHandler) share
 * one macro-parameter policy and one raw-gcode fallback instead of three.
 *
 * Tier 1 stays with the callers: the backend call is inseparable from each
 * surface's own guard/stepper/spinner bookkeeping.
 */

#pragma once

#include "macro_param_cache.h"
#include "macro_param_modal.h"

#include <functional>
#include <string>

namespace helix::ui {

/**
 * @brief The process-wide MacroParamModal every filament surface shares.
 *
 * A function-local static, exactly as it was when it lived in
 * ui_panel_filament.cpp: one instance for the whole app, alive for the process
 * lifetime.
 *
 * That lifetime is load-bearing and easy to miss. MacroParamModal stores its
 * on_execute_ callback and does NOT clear it on dismiss — only the next
 * show_for_*() overwrites it. So a callback handed to this modal can fire
 * arbitrarily later, long after the object that built it is gone. FilamentPanel
 * is an immortal singleton and may capture [this] directly [L012]. Every other
 * caller MUST route through an AsyncLifetimeGuard token; AmsOperationSidebar is
 * destroyed whenever the AMS panel closes, which is a live use-after-free
 * without one.
 */
helix::MacroParamModal& get_filament_param_modal();

/// Whether a tier-2 dispatch may interrupt the user to collect parameters.
enum class ParamPolicy {
    /// Raise MacroParamModal when the macro takes (or may take) parameters.
    Prompt,
    /// Run with no parameters and never raise a modal. Required for any surface
    /// that already owns a dialog — a second modal would stack on top of a live
    /// one whose observers keep firing underneath it.
    Suppress,
};

/**
 * @brief How a parameter prompt is raised.
 *
 * The default installs get_filament_param_modal() on lv_screen_active(). It is
 * an injection point so the prompt branch is reachable in a test binary with no
 * screen, and so a future surface can present parameters its own way without
 * forking dispatch_filament_macro().
 */
using ParamPrompter =
    std::function<void(const std::string& macro_name, const helix::CachedMacroInfo& cached,
                       helix::MacroExecuteCallback on_execute)>;

/// Install a prompter. Pass a default-constructed ParamPrompter to restore the
/// shared-modal default.
void set_filament_param_prompter(ParamPrompter prompter);

/**
 * @brief How the "toolhead isn't homed, inject G28?" confirmation is raised.
 *
 * Sibling seam to ParamPrompter above, for the same reason: it makes the
 * confirm-before-homing branch (AmsSubscriptionBackend::ensure_homed_then())
 * reachable from a headless test binary, and lets the one real caller
 * (SubjectInitializer) present it as a modal without ensure_homed_then()
 * knowing anything about LVGL.
 */
using HomeConfirmPrompter =
    std::function<void(std::function<void()> on_confirm, std::function<void()> on_cancel)>;

/// Install a prompter. Pass a default-constructed HomeConfirmPrompter to
/// restore the default described on request_home_confirmation().
void set_home_confirm_prompter(HomeConfirmPrompter prompter);

/**
 * @brief Ask before ensure_homed_then() injects an unrequested G28.
 *
 * With no prompter installed -- the state of every test that doesn't call
 * set_home_confirm_prompter(), and ~4600 of them don't -- @p on_confirm fires
 * immediately and synchronously. That default is load-bearing: it is what
 * keeps every pre-existing homing test (and every un-migrated caller) seeing
 * today's "just home it" behaviour with no prompter wired up.
 */
void request_home_confirmation(std::function<void()> on_confirm, std::function<void()> on_cancel);

/**
 * @brief Tier 2: dispatch the user's configured macro.
 *
 * Resolves @p macro_name against MacroParamCache. Under ParamPolicy::Prompt a
 * macro with KNOWN_PARAMS or UNKNOWN parameters raises the prompter and @p run
 * fires only if the user confirms; under ParamPolicy::Suppress @p run always
 * fires immediately with an empty MacroParamResult.
 *
 * @warning Under ParamPolicy::Prompt, @p run outlives this call and is retained
 *          by the shared modal past dismissal. Callers that are not immortal
 *          must capture an AsyncLifetimeGuard token in @p run, not a bare
 *          `this`.
 *
 * @return true if a prompt was raised (@p run fires later, or never), false if
 *         @p run was invoked synchronously with no parameters.
 */
bool dispatch_filament_macro(const std::string& macro_name, ParamPolicy policy,
                             helix::MacroExecuteCallback run);

/**
 * @brief Tier 3 load fallback: fast move through the bowden, then a slow push
 *        into the melt zone. M83 = relative extrusion.
 */
[[nodiscard]] std::string filament_load_fallback_gcode();

/**
 * @brief Tier 3 unload fallback: tip-shape (push, quick pull, dwell) then a
 *        long retract. M83 = relative extrusion.
 */
[[nodiscard]] std::string filament_unload_fallback_gcode();

/**
 * @brief Purge fallback: extrude a fixed 50mm at 10mm/s. M83 = relative
 *        extrusion. Purge has no plan_purge()/AmsBackend tier, so this is the
 *        whole tier-2 ladder alongside the configured macro, not "tier 3" of
 *        three.
 */
[[nodiscard]] std::string filament_purge_fallback_gcode();

} // namespace helix::ui
