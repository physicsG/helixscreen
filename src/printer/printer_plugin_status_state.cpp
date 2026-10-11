// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file printer_plugin_status_state.cpp
 * @brief HelixPrint plugin status management extracted from PrinterState
 *
 * Manages plugin installation subjects for UI feature gating.
 * Uses tri-state semantics (-1=unknown, 0=no, 1=yes) to distinguish between
 * "still checking" and "definitely not available" states.
 *
 * Extracted from PrinterState as part of god class decomposition.
 */

#include "printer_plugin_status_state.h"

#include "macro_manager.h"
#include "state/subject_macros.h"

#include <spdlog/spdlog.h>

namespace helix {

void PrinterPluginStatusState::init_subjects(bool register_xml) {
    if (subjects_initialized_) {
        spdlog::debug("[PrinterPluginStatusState] Subjects already initialized, skipping");
        return;
    }

    spdlog::trace("[PrinterPluginStatusState] Initializing subjects (register_xml={})",
                  register_xml);

    // Plugin status subjects use tri-state: -1=unknown, 0=no, 1=yes
    // Unknown state allows UI to show "checking..." vs "not available"
    INIT_SUBJECT_INT(helix_plugin_installed, -1, subjects_, register_xml);
    INIT_SUBJECT_INT(helix_macros_status, static_cast<int>(HelixMacrosStatus::Unknown), subjects_,
                     register_xml);

    // Fresh subjects mean a fresh session: re-initialization (test fixtures
    // re-init the shared PrinterState between cases) must not read a base
    // status or pending flag from the previous one.
    macros_base_status_ = static_cast<int>(HelixMacrosStatus::Unknown);
    macros_restart_pending_ = false;

    subjects_initialized_ = true;
    spdlog::trace("[PrinterPluginStatusState] Subjects initialized successfully");
}

void PrinterPluginStatusState::deinit_subjects() {
    if (!subjects_initialized_) {
        return;
    }

    spdlog::debug("[PrinterPluginStatusState] Deinitializing subjects");

    subjects_.deinit_all();
    subjects_initialized_ = false;
}

void PrinterPluginStatusState::set_installed(bool installed) {
    lv_subject_set_int(&helix_plugin_installed_, installed ? 1 : 0);
    spdlog::info("[PrinterPluginStatusState] HelixPrint plugin installed: {}", installed);
}

void PrinterPluginStatusState::set_helix_macros_base_status(HelixMacrosStatus base) {
    macros_base_status_ = static_cast<int>(base);
    if (base == HelixMacrosStatus::Installed) {
        // Discovery sees the CURRENT pack active: a Klipper restart landed and
        // any staged install/update offer is resolved. An Outdated base does
        // NOT clear the flag — until the restart, discovery still reports the
        // old rung, which is exactly the state the flag describes.
        macros_restart_pending_ = false;
    }
    publish_helix_macros_status();
    spdlog::info("[PrinterPluginStatusState] Helper macros base status: {}", macros_base_status_);
}

void PrinterPluginStatusState::set_helix_macros_base_status(MacroInstallStatus install) {
    switch (install) {
    case MacroInstallStatus::NOT_INSTALLED:
        set_helix_macros_base_status(HelixMacrosStatus::NotInstalled);
        break;
    case MacroInstallStatus::INSTALLED:
        set_helix_macros_base_status(HelixMacrosStatus::Installed);
        break;
    case MacroInstallStatus::OUTDATED:
        set_helix_macros_base_status(HelixMacrosStatus::Outdated);
        break;
    case MacroInstallStatus::UNKNOWN:
        set_helix_macros_base_status(HelixMacrosStatus::Unknown);
        break;
    }
}

void PrinterPluginStatusState::set_helix_macros_restart_pending(bool pending) {
    macros_restart_pending_ = pending;
    publish_helix_macros_status();
    spdlog::info("[PrinterPluginStatusState] Helper macro restart pending: {}", pending);
}

void PrinterPluginStatusState::publish_helix_macros_status() {
    int composed = macros_base_status_;
    // RestartPending masks both staging bases: NotInstalled (fresh install)
    // and Outdated (update whose new pack is not loaded yet). Installed is
    // never masked — macros active at the current version is the one state
    // where nothing can be pending.
    if (macros_restart_pending_ &&
        (macros_base_status_ == static_cast<int>(HelixMacrosStatus::NotInstalled) ||
         macros_base_status_ == static_cast<int>(HelixMacrosStatus::Outdated))) {
        composed = static_cast<int>(HelixMacrosStatus::RestartPending);
    }
    lv_subject_set_int(&helix_macros_status_, composed);
}

} // namespace helix
