// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "safety_settings_manager.h"

#include "ui_notification_threshold.h"

#include "config.h"
#include "spdlog/spdlog.h"
#include "static_subject_registry.h"

#include <algorithm>

using namespace helix;

static constexpr int ESCALATION_TIMEOUT_VALUES[] = {15, 30, 60, 120};

using settings::Scope;
// Row order is SafetySettingsManager::Key.
static constexpr settings::PersistedSetting SAFETY_SETTINGS[] = {
    {"settings_estop_confirm", "/safety/estop_require_confirmation", Scope::Global, true, 1, 0, 1,
     nullptr},
    {"settings_cancel_escalation_enabled", "/safety/cancel_escalation_enabled", Scope::Global, true,
     0, 0, 1, nullptr},
    {"settings_macro_confirm", "/safety/macro_require_confirmation", Scope::Global, true, 1, 0, 1,
     nullptr},
    {"settings_allow_cold_extrude", "/safety/allow_cold_extrude", Scope::Global, true, 0, 0, 1,
     nullptr},
};

SafetySettingsManager& SafetySettingsManager::instance() {
    static SafetySettingsManager instance;
    return instance;
}

SafetySettingsManager::SafetySettingsManager() : settings_(SAFETY_SETTINGS) {
    spdlog::trace("[SafetySettingsManager] Constructor");
}

void SafetySettingsManager::init_subjects() {
    if (subjects_initialized_) {
        spdlog::debug("[SafetySettingsManager] Subjects already initialized, skipping");
        return;
    }

    spdlog::debug("[SafetySettingsManager] Initializing subjects");

    Config* config = Config::get_instance();

    settings_.init(subjects_);

    // Cancel escalation timeout (default: 30s, stored as dropdown index 0-3)
    int cancel_escalation_timeout =
        config->get<int>("/safety/cancel_escalation_timeout_seconds", 30);
    // Convert seconds to dropdown index: 15->0, 30->1, 60->2, 120->3
    int timeout_index = 1; // default 30s
    if (cancel_escalation_timeout <= 15)
        timeout_index = 0;
    else if (cancel_escalation_timeout <= 30)
        timeout_index = 1;
    else if (cancel_escalation_timeout <= 60)
        timeout_index = 2;
    else
        timeout_index = 3;
    UI_MANAGED_SUBJECT_INT(cancel_escalation_timeout_subject_, timeout_index,
                           "settings_cancel_escalation_timeout", subjects_);

    // Minimum toast severity (default: 0 = All, no change for existing users). #1213.
    int min_toast_severity = config->get<int>("/notifications/min_toast_severity", 0);
    min_toast_severity =
        (min_toast_severity == 1 || min_toast_severity == 2) ? min_toast_severity : 0;
    UI_MANAGED_SUBJECT_INT(min_toast_severity_subject_, min_toast_severity,
                           "settings_min_toast_severity", subjects_);
    // Push the persisted value to the toast gate (header-only inline cache).
    helix::ui::notifications::set_min_toast_severity_cache(min_toast_severity);

    subjects_initialized_ = true;

    // Self-register cleanup with StaticSubjectRegistry
    StaticSubjectRegistry::instance().register_deinit(
        "SafetySettingsManager", []() { SafetySettingsManager::instance().deinit_subjects(); });

    spdlog::debug("[SafetySettingsManager] Subjects initialized: timeout_index={}", timeout_index);
}

void SafetySettingsManager::deinit_subjects() {
    if (!subjects_initialized_) {
        return;
    }

    spdlog::trace("[SafetySettingsManager] Deinitializing subjects");
    subjects_.deinit_all();
    subjects_initialized_ = false;
    spdlog::trace("[SafetySettingsManager] Subjects deinitialized");
}

// =============================================================================
// GETTERS / SETTERS
// =============================================================================

int SafetySettingsManager::get_cancel_escalation_timeout_seconds() const {
    int index = lv_subject_get_int(const_cast<lv_subject_t*>(&cancel_escalation_timeout_subject_));
    index = std::max(0, std::min(3, index));
    return ESCALATION_TIMEOUT_VALUES[index];
}

void SafetySettingsManager::set_cancel_escalation_timeout_seconds(int seconds) {
    spdlog::info("[SafetySettingsManager] set_cancel_escalation_timeout_seconds({})", seconds);

    // Convert seconds to dropdown index
    int index = 1; // default 30s
    if (seconds <= 15)
        index = 0;
    else if (seconds <= 30)
        index = 1;
    else if (seconds <= 60)
        index = 2;
    else
        index = 3;

    lv_subject_set_int(&cancel_escalation_timeout_subject_, index);

    Config* config = Config::get_instance();
    config->set<int>("/safety/cancel_escalation_timeout_seconds", ESCALATION_TIMEOUT_VALUES[index]);
    config->save();

    spdlog::debug(
        "[SafetySettingsManager] Cancel escalation timeout set to {}s (index {}) and saved",
        ESCALATION_TIMEOUT_VALUES[index], index);
}

int SafetySettingsManager::get_min_toast_severity() const {
    return lv_subject_get_int(const_cast<lv_subject_t*>(&min_toast_severity_subject_));
}

void SafetySettingsManager::set_min_toast_severity(int index) {
    int clamped = (index == 1 || index == 2) ? index : 0;
    spdlog::info("[SafetySettingsManager] set_min_toast_severity({} -> {})", index, clamped);

    lv_subject_set_int(&min_toast_severity_subject_, clamped);

    Config* config = Config::get_instance();
    config->set<int>("/notifications/min_toast_severity", clamped);
    config->save();

    // Keep the toast gate in sync so the change takes effect immediately.
    helix::ui::notifications::set_min_toast_severity_cache(clamped);

    spdlog::debug("[SafetySettingsManager] Min toast severity set to index {} and saved", clamped);
}
