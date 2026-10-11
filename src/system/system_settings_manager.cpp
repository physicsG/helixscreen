// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "system_settings_manager.h"

#include "cjk_font_manager.h"
#include "config.h"
#include "locale_formats.h"
#include "logging_init.h"
#include "lvgl/src/others/translation/lv_translation.h"
#include "spdlog/spdlog.h"
#include "static_subject_registry.h"
#include "system/crash_handler.h"
#include "system/telemetry_manager.h"
#include "system/update_checker.h"
#include "translation_loader.h"

#include <algorithm>
#include <string_view>

using namespace helix;

// Log level options (dropdown index → spdlog level)
// Order: Warn=0, Info=1, Debug=2, Trace=3
static const spdlog::level::level_enum LOG_LEVEL_VALUES[] = {
    spdlog::level::warn,
    spdlog::level::info,
    spdlog::level::debug,
    spdlog::level::trace,
};
static const int LOG_LEVEL_COUNT = sizeof(LOG_LEVEL_VALUES) / sizeof(LOG_LEVEL_VALUES[0]);

// Config key strings matching the dropdown indices
static const char* LOG_LEVEL_STRINGS[] = {"warn", "info", "debug", "trace"};

static int spdlog_level_to_index(spdlog::level::level_enum level) {
    for (int i = 0; i < LOG_LEVEL_COUNT; ++i) {
        if (LOG_LEVEL_VALUES[i] == level) {
            return i;
        }
    }
    return 0; // Default to Warn
}

#ifndef HELIX_HAS_CJK
#define HELIX_HAS_CJK 1
#endif

// Language options - codes and display names
// Order: en, de, fr, es, ru, pt, it, zh, ja (indices 0-8). The CJK pair comes
// last so a build without CJK fonts (HELIX_HAS_CJK=0, the ESP32 firmware) drops
// it by shortening the list, and every other index keeps its meaning. A saved
// zh/ja there reads as English.
static const char* LANGUAGE_CODES[] = {"en", "de", "fr", "es", "ru", "pt", "it", "zh", "ja"};
static const int LANGUAGE_COUNT = HELIX_HAS_CJK ? 9 : 7;
static const char* LANGUAGE_OPTIONS_TEXT =
    HELIX_HAS_CJK
        ? "English\nDeutsch\nFrançais\nEspañol\nРусский\nPortuguês\nItaliano\n中文\n日本語"
        : "English\nDeutsch\nFrançais\nEspañol\nРусский\nPortuguês\nItaliano";

using settings::Scope;
// Row order is SystemSettingsManager::Key.
static constexpr settings::PersistedSetting SYSTEM_SETTINGS[] = {
    // 0=Stable, 1=Beta, 2=Dev
    {"update_channel", "/update/channel", Scope::Global, false, 0, 0, 2, nullptr},
    // Opt-in, default off
    {"settings_telemetry_enabled", "/telemetry_enabled", Scope::Global, true, 0, 0, 1, nullptr},
    // Default on: a device that has never seen this setting comes up with WiFi enabled
    {"settings_wifi_enabled", "/wifi_enabled", Scope::Global, true, 1, 0, 1, nullptr},
};

SystemSettingsManager& SystemSettingsManager::instance() {
    static SystemSettingsManager instance;
    return instance;
}

SystemSettingsManager::SystemSettingsManager() : settings_(SYSTEM_SETTINGS) {
    spdlog::trace("[SystemSettingsManager] Constructor");
}

void SystemSettingsManager::init_subjects() {
    if (subjects_initialized_) {
        spdlog::debug("[SystemSettingsManager] Subjects already initialized, skipping");
        return;
    }

    spdlog::debug("[SystemSettingsManager] Initializing subjects");

    Config* config = Config::get_instance();

    // Language (default: "en" = English, index 0)
    std::string lang_code = config->get_language();
    int lang_index = language_code_to_index(lang_code);
    UI_MANAGED_SUBJECT_INT(language_subject_, lang_index, "settings_language", subjects_);
    spdlog::debug("[SystemSettingsManager] Language initialized to {} (index {})", lang_code,
                  lang_index);

    // Initialize locale formatting for the loaded language
    helix::ui::locale_set_language(lang_code);

    settings_.init(subjects_);

    // Log level. With nothing saved, report the level the persistent sinks
    // actually run at. spdlog::get_level() is the logger's floor, which sits at
    // debug whenever the ring-buffer sink is capturing debug for the debug
    // bundle, so it reads "debug" on a device logging at warn.
    std::string config_log_level = config->get<std::string>("/log_level", "");
    int log_level_index;
    if (!config_log_level.empty()) {
        auto level = helix::logging::parse_level(config_log_level, spdlog::level::warn);
        log_level_index = spdlog_level_to_index(level);
    } else {
        log_level_index = spdlog_level_to_index(helix::logging::effective_log_level());
    }
    UI_MANAGED_SUBJECT_INT(log_level_subject_, log_level_index, "settings_log_level", subjects_);
    spdlog::debug("[SystemSettingsManager] log_level: {} (index {})",
                  LOG_LEVEL_STRINGS[log_level_index], log_level_index);

    subjects_initialized_ = true;

    // Self-register cleanup with StaticSubjectRegistry
    StaticSubjectRegistry::instance().register_deinit(
        "SystemSettingsManager", []() { SystemSettingsManager::instance().deinit_subjects(); });

    spdlog::debug("[SystemSettingsManager] Subjects initialized: language={}", lang_code);
}

void SystemSettingsManager::deinit_subjects() {
    if (!subjects_initialized_) {
        return;
    }

    spdlog::trace("[SystemSettingsManager] Deinitializing subjects");
    subjects_.deinit_all();
    subjects_initialized_ = false;
    spdlog::trace("[SystemSettingsManager] Subjects deinitialized");
}

// =============================================================================
// LANGUAGE SETTINGS
// =============================================================================

std::string SystemSettingsManager::get_language() const {
    int index = lv_subject_get_int(const_cast<lv_subject_t*>(&language_subject_));
    return language_index_to_code(index);
}

void SystemSettingsManager::set_language(const std::string& lang) {
    int index = language_code_to_index(lang);
    spdlog::info("[SystemSettingsManager] set_language({}) -> index {}", lang, index);
    crash_handler::breadcrumb::note("lang", lang.c_str(), static_cast<long>(index));

    // 1. Update subject (UI reacts)
    lv_subject_set_int(&language_subject_, index);

    // 2. Ensure the target locale's translation pack is loaded. Startup loads
    //    only the saved lang; subsequent switches pull in additional packs.
    helix::ui::ensure_translation_loaded(lang);

    // 3. Call LVGL translation API for hot-reload
    // This sends LV_EVENT_TRANSLATION_LANGUAGE_CHANGED to all widgets
    crash_handler::breadcrumb::note("lang", "lv_translation_begin");
    lv_translation_set_language(lang.c_str());
    crash_handler::breadcrumb::note("lang", "lv_translation_end");

    // Load/unload CJK runtime fonts based on locale
    helix::system::CjkFontManager::instance().on_language_changed(lang);

    // Subject strings translated when they were written re-render here
    UpdateChecker::instance().on_language_changed();

    // 3. Update locale formatting tables
    helix::ui::locale_set_language(lang);

    // 4. Persist to config
    Config* config = Config::get_instance();
    config->set_language(lang);
    config->save();
    crash_handler::breadcrumb::note("lang", "set_end", static_cast<long>(index));
}

void SystemSettingsManager::set_language_by_index(int index) {
    std::string code = language_index_to_code(index);
    set_language(code);
}

int SystemSettingsManager::get_language_index() const {
    return lv_subject_get_int(const_cast<lv_subject_t*>(&language_subject_));
}

const char* SystemSettingsManager::get_language_options() {
    return LANGUAGE_OPTIONS_TEXT;
}

std::string SystemSettingsManager::get_language_display_name() const {
    std::string_view options(LANGUAGE_OPTIONS_TEXT);
    size_t pos = 0;
    for (int i = 0; i < get_language_index() && pos != std::string_view::npos; ++i) {
        pos = options.find('\n', pos);
        if (pos != std::string_view::npos) {
            ++pos;
        }
    }
    if (pos == std::string_view::npos) {
        return "";
    }
    size_t end = options.find('\n', pos);
    return std::string(
        options.substr(pos, end == std::string_view::npos ? std::string_view::npos : end - pos));
}

std::string SystemSettingsManager::language_index_to_code(int index) {
    if (index < 0 || index >= LANGUAGE_COUNT) {
        return "en"; // Default to English
    }
    return LANGUAGE_CODES[index];
}

int SystemSettingsManager::language_code_to_index(const std::string& code) {
    for (int i = 0; i < LANGUAGE_COUNT; ++i) {
        if (code == LANGUAGE_CODES[i]) {
            return i;
        }
    }
    return 0; // Default to English (index 0)
}

// =============================================================================
// UPDATE CHANNEL SETTINGS
// =============================================================================

void SystemSettingsManager::set_update_channel(int channel) {
    settings_.set(Key::UpdateChannel, channel);
    // Force a re-check on the new channel
    UpdateChecker::instance().clear_cache();
}

// =============================================================================
// TELEMETRY SETTINGS
// =============================================================================

void SystemSettingsManager::set_telemetry_enabled(bool enabled) {
    settings_.set(Key::TelemetryEnabled, enabled);
    TelemetryManager::instance().set_enabled(enabled);
}

// =============================================================================
// LOG LEVEL SETTINGS
// =============================================================================

int SystemSettingsManager::get_log_level_index() const {
    return lv_subject_get_int(const_cast<lv_subject_t*>(&log_level_subject_));
}

void SystemSettingsManager::set_log_level_by_index(int index) {
    int clamped = std::clamp(index, 0, LOG_LEVEL_COUNT - 1);
    spdlog::info("[SystemSettingsManager] set_log_level_by_index({}) -> {}", clamped,
                 LOG_LEVEL_STRINGS[clamped]);

    // 1. Update subject (UI reacts)
    lv_subject_set_int(&log_level_subject_, clamped);

    // 2. Apply immediately — no restart needed
    helix::logging::set_runtime_level(LOG_LEVEL_VALUES[clamped]);

    // 3. Persist to config
    Config* config = Config::get_instance();
    config->set<std::string>("/log_level", LOG_LEVEL_STRINGS[clamped]);
    config->save();
}
