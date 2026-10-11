// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "subject_managed_panel.h"

#include <lvgl.h>

namespace helix {

enum class MacroInstallStatus;

/**
 * @brief UI-facing status of the HelixScreen helper macro pack
 *
 * Extends the printer-side detection states (MacroInstallStatus) with
 * RESTART_PENDING — the post-staging truth: the files are on the printer
 * but the macros only load at the next Klipper restart. Numeric values are
 * the helix_macros_status subject values the XML rows bind against.
 */
enum class HelixMacrosStatus {
    Unknown = -1,       ///< No objects list from the printer yet
    NotInstalled = 0,   ///< Discovered; the printer has no Helix macros
    Installed = 1,      ///< Current pack version active
    Outdated = 2,       ///< Older pack active; the update offer applies
    RestartPending = 3, ///< Files staged; activate at the next Klipper restart
};

/**
 * @brief Manages HelixPrint plugin status subjects for UI feature gating
 *
 * Tracks whether the HelixPrint Klipper plugin is installed and the install
 * status of the HelixScreen helper macro pack. The plugin subjects use
 * tri-state semantics: -1=unknown, 0=not installed, 1=installed.
 *
 * The macro status subject composes a discovery-derived base value with a
 * restart-pending flag set by the install flow (see HelixMacrosStatus).
 *
 * The unknown (-1) state allows the UI to distinguish between:
 * - "Still checking" (show loading/spinner)
 * - "Definitely not available" (show install prompt)
 * - "Available" (show feature controls)
 *
 * @note set_helix_plugin_installed triggers composite visibility updates
 *       in PrinterState (the has_any_preprint_options aggregate subject).
 */
class PrinterPluginStatusState {
  public:
    PrinterPluginStatusState() = default;
    ~PrinterPluginStatusState() = default;

    // Non-copyable
    PrinterPluginStatusState(const PrinterPluginStatusState&) = delete;
    PrinterPluginStatusState& operator=(const PrinterPluginStatusState&) = delete;

    /**
     * @brief Initialize plugin status subjects
     * @param register_xml If true, register subjects with LVGL XML system
     */
    void init_subjects(bool register_xml = true);

    /**
     * @brief Deinitialize subjects (called by SubjectManager automatically)
     */
    void deinit_subjects();

    // ========================================================================
    // Setters
    // ========================================================================

    /**
     * @brief Set helix plugin installed status
     *
     * Called from within helix::ui::queue_update() by PrinterState, which
     * handles the async dispatch and the subsequent visibility update.
     *
     * @param installed True if HelixPrint plugin is installed
     */
    void set_installed(bool installed);

    /**
     * @brief Set the discovery-derived helper-macro install status
     *
     * Called from PrinterState::set_hardware() on the main thread once a
     * discovery snapshot has been folded in. Only an Installed base clears
     * the restart-pending flag: discovery reporting the CURRENT pack active
     * means a restart landed. An Outdated base keeps it — until the restart,
     * discovery still reports the old rung.
     *
     * @param base Status derived via MacroManager::evaluate_status()
     */
    void set_helix_macros_base_status(HelixMacrosStatus base);

    /// The base status for a MacroManager::evaluate_status() verdict. An
    /// Installed verdict clears a restart-pending flag held for a staged
    /// install or update; Outdated keeps it, since the staged pack is not
    /// loaded until the restart.
    void set_helix_macros_base_status(MacroInstallStatus install);

    /**
     * @brief Mark helper-macro files as staged and awaiting a Klipper restart
     *
     * Set by the install flow after install_files()/update_files() succeeded
     * while a print made an immediate restart unsafe; cleared when discovery
     * reports the macros active at the current version. Main thread only
     * (fired from deferred callbacks).
     *
     * @param pending True while the staged files still await a restart
     */
    void set_helix_macros_restart_pending(bool pending);

    // ========================================================================
    // Subject accessors
    // ========================================================================

    /// Tri-state: -1=unknown, 0=not installed, 1=installed
    lv_subject_t* get_helix_plugin_installed_subject() {
        return &helix_plugin_installed_;
    }

    /// HelixMacrosStatus value the XML rows bind against
    lv_subject_t* get_helix_macros_status_subject() {
        return &helix_macros_status_;
    }

    // ========================================================================
    // Query methods
    // ========================================================================

    /**
     * @brief Plugin presence as published, without collapsing the unknown
     *
     * @return 1 installed, 0 absent, -1 not probed yet. Callers that must tell
     *         "not probed" apart from "absent" want this one.
     */
    int helix_plugin_state() const {
        return lv_subject_get_int(const_cast<lv_subject_t*>(&helix_plugin_installed_));
    }

    /**
     * @brief Check if HelixPrint plugin is installed
     *
     * @return true only when value is 1 (installed), false for -1 (unknown) or 0 (not installed)
     */
    bool service_has_helix_plugin() const {
        return helix_plugin_state() == 1;
    }

  private:
    SubjectManager subjects_;
    bool subjects_initialized_ = false;

    // Plugin status subjects (tri-state: -1=unknown, 0=no, 1=yes)
    lv_subject_t helix_plugin_installed_{}; // HelixPrint Klipper plugin

    /// Composed HelixMacrosStatus; see publish_helix_macros_status()
    lv_subject_t helix_macros_status_{};
    int macros_base_status_ = static_cast<int>(HelixMacrosStatus::Unknown);
    bool macros_restart_pending_ = false;

    /// RestartPending only masks NotInstalled: once discovery reports the
    /// macros active, the pending flag is stale by definition.
    void publish_helix_macros_status();
};

} // namespace helix
