// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "translation_loader.h"

#include "data_root_resolver.h"

#include <spdlog/spdlog.h>

#include <lvgl.h>
#include <set>
#include <string>

namespace helix::ui {

namespace {
// Tracks which locales have been registered with LVGL this session. LVGL
// exposes no lv_translation_remove_pack — packs accumulate until deinit.
// The set exists to skip redundant file reads on repeated switches to the
// same locale.
std::set<std::string>& loaded_locales() {
    static std::set<std::string> s;
    return s;
}
} // namespace

void ensure_translation_loaded(const std::string& lang) {
    if (lang.empty())
        return;

    // Earlier revision short-circuited for "en" on the theory that tags ARE
    // English and lv_translation_get returns the tag when no pack matches the
    // selected language. That's functionally correct but makes every lv_tr()
    // call log `language is not found` — hundreds of warnings on the device.
    // Loading en.xml costs ~140 KB of heap; tolerable vs the log spam.

    if (loaded_locales().count(lang) > 0)
        return;

    std::string path = "A:" + helix::asset_path("ui_xml/translations/" + lang + ".xml");
    lv_result_t res = lv_xml_register_translation_from_file(path.c_str());
    if (res != LV_RESULT_OK) {
        spdlog::warn("[TranslationLoader] Failed to load '{}' — UI will fall back to English",
                     path);
        return;
    }

    loaded_locales().insert(lang);
    spdlog::debug("[TranslationLoader] Loaded translation pack for '{}'", lang);
}

} // namespace helix::ui
