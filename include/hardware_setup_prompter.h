// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "i_moonraker_api.h"
#include "lvgl/lvgl.h"
#include "wizard_step.h" // helix::wizard::StepId

#include <functional>
#include <vector>

namespace helix {

class AsyncLifetimeGuard;
class PrinterDiscovery;

/// Which hardware prompt a discovery pass launches. At most one per pass: the gates
/// below make the three reasons mutually exclusive.
enum class HardwarePrompt {
    None,
    Reconfig,       ///< targeted wizard for guided roles with no live match
    DeferredOffer,  ///< offer the hardware steps a Klipper-down setup skipped
    DeferredSilent, ///< that debt, but nothing is left to ask: settle it quietly
    TypeMismatch,   ///< saved printer type contradicts detected hardware
};

/// Everything choose_prompt() reads. All of it is known by the time the discovery
/// pass reaches its prompts.
struct HardwarePromptGates {
    bool hw_changed = false;              ///< hardware shape differs from the last discovery
    bool print_active = false;            ///< a print is running, so no wizard may open over it
    bool wizard_blocked = false;          ///< the first-run wizard is required or running
    bool reconfig_steps_pending = false;  ///< guided roles left unresolved
    bool hardware_setup_deferred = false; ///< a Klipper-down setup left steps behind
    bool reconfig_shown = false;          ///< reconfig already launched this connection
    bool deferred_prompt_shown = false;   ///< deferred offer already made this session
    bool type_mismatch_shown = false;     ///< mismatch already prompted this session
};

/// Pure decision for the discovery-time prompt. @p deferred_steps_empty is consulted
/// only when a deferred offer is otherwise due: computing the steps has side effects
/// on filament-sensor discovery, so the answer must not be fetched when nothing could
/// use it.
HardwarePrompt choose_prompt(const HardwarePromptGates& gates,
                             const std::function<bool()>& deferred_steps_empty);

/// Owns the hardware-setup prompts a discovery pass can raise (reconfig wizard, the
/// deferred-setup offer, the type-mismatch warning), the guards that keep each to
/// once per session or connection, and the wizard sessions they launch. Main thread.
class HardwareSetupPrompter {
  public:
    /// @p screen and @p api are read at use, since both change across a printer switch.
    HardwareSetupPrompter(AsyncLifetimeGuard& lifetime, std::function<lv_obj_t*()> screen,
                          std::function<IMoonrakerAPI*()> api)
        : lifetime_(lifetime), screen_(std::move(screen)), api_(std::move(api)) {}

    /// A discovery cycle is starting: a reconnect may offer the reconfig wizard again.
    void begin_discovery_cycle() {
        guards.reconfig_shown = false;
    }

    /// Once-per-session and once-per-connection guards. Public so a test can prove a
    /// printer switch re-arms them.
    struct Guards {
        /// The discovery-triggered targeted reconfig wizard launches at most once per
        /// connection; re-armed when a new discovery cycle begins.
        bool reconfig_shown = false;
        /// The deferred hardware-setup offer (#1160) is made at most once per session, so
        /// a reconnect cannot re-ask. The persisted per-printer marker stops it across
        /// sessions: cleared as soon as the user answers either way.
        bool deferred_prompt_shown = false;
        /// The saved-vs-detected type mismatch warning shows at most once per session,
        /// whichever button dismisses it. The persisted TYPE_MISMATCH_SHOWN_FOR flag covers
        /// cross-boot. Not re-armed on reconnect, only on a new printer.
        bool type_mismatch_shown = false;
    };
    Guards guards;

    /// Re-arm every guard and drop pending wizard steps. A new printer's first
    /// discovery is a first discovery with its own prompts to show.
    void reset_for_new_connection();

    /// The prompt pass of a completed discovery. Launches at most one prompt.
    /// @param hw discovered hardware (already stored in the API)
    /// @return which prompt was launched
    HardwarePrompt run_discovery_prompts(const PrinterDiscovery& hw, bool hw_changed,
                                         bool print_active, bool hardware_setup_deferred);

    /// Re-resolve and persist fan AND heater roles, then rebind fan UI to the new
    /// mapping, after a targeted wizard session finishes. Marshals to the main thread
    /// internally; safe to call from a main-thread on_complete.
    void reapply_hardware_roles();

  private:
    void launch_reconfig_wizard(std::vector<wizard::StepId> steps);
    void prompt_deferred_hardware_setup(std::vector<wizard::StepId> steps);
    void launch_deferred_hardware_setup();
    void settle_deferred_hardware_setup();
    void maybe_warn_type_mismatch(const PrinterDiscovery& hardware);
    void launch_type_reidentify_wizard();
    void settle_type_mismatch_warning();

    AsyncLifetimeGuard& lifetime_;
    std::function<lv_obj_t*()> screen_;
    std::function<IMoonrakerAPI*()> api_;

    // Steps the deferred offer runs if accepted, held for the confirm callback's timer.
    std::vector<wizard::StepId> pending_steps_;
};

} // namespace helix
