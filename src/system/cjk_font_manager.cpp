// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "cjk_font_manager.h"

#include "ui_fonts.h"

#include "spdlog/spdlog.h"

#ifndef HELIX_MAX_FONT_TIER
#define HELIX_MAX_FONT_TIER 6 // default: all tiers (micro=0 .. xxlarge=6)
#endif
#ifndef HELIX_HAS_HIDPI_FONTS
#define HELIX_HAS_HIDPI_FONTS 0 // conservative: assume the 48/64 faces are not linked
#endif

namespace helix::system {

struct FontMapping {
    lv_font_t* compiled;
    const char* bin_filename;
};

// NOTE: xlarge (≥5) and xxlarge (≥6) font entries are guarded with
// HELIX_MAX_FONT_TIER checks because those font symbols are pruned from the
// link on constrained platforms (e.g. AD5M ships only medium+large tiers). The
// 48/64 regular/bold and 32/40 light faces need a second, narrower guard,
// HELIX_HAS_HIDPI_FONTS: a platform can reach tier 6 (ESP32 does) while still
// not linking those high-DPI-only rungs, matching the same flag's use in
// asset_manager.cpp register_xxlarge_tier_fonts() and
// ui_wizard_language_chooser.cpp. Taking either guard's faces' address
// unconditionally would produce undefined references.
// clang-format off
static const FontMapping REGULAR_FONTS[] = {
    {&noto_sans_8, "noto_sans_cjk_8.bin"},
    {&noto_sans_10, "noto_sans_cjk_10.bin"},
    {&noto_sans_11, "noto_sans_cjk_11.bin"},
    {&noto_sans_12, "noto_sans_cjk_12.bin"},
    {&noto_sans_14, "noto_sans_cjk_14.bin"},
    {&noto_sans_16, "noto_sans_cjk_16.bin"},
    {&noto_sans_18, "noto_sans_cjk_18.bin"},
    {&noto_sans_20, "noto_sans_cjk_20.bin"},
    {&noto_sans_24, "noto_sans_cjk_24.bin"},
#if HELIX_MAX_FONT_TIER >= 3
    {&noto_sans_26, "noto_sans_cjk_26.bin"},
#endif
#if HELIX_MAX_FONT_TIER >= 4
    {&noto_sans_28, "noto_sans_cjk_28.bin"},
#endif
#if HELIX_MAX_FONT_TIER >= 5
    {&noto_sans_32, "noto_sans_cjk_32.bin"},
#endif
#if HELIX_MAX_FONT_TIER >= 6
    {&noto_sans_40, "noto_sans_cjk_40.bin"},
#if HELIX_HAS_HIDPI_FONTS
    // 48/64 are the high-DPI scale rungs (DisplayMetrics::scaled_font_name).
    // A platform that reaches tier 6 without linking them (HELIX_HAS_HIDPI_FONTS=0,
    // e.g. ESP32) has no compiled symbol to take the address of.
    {&noto_sans_48, "noto_sans_cjk_48.bin"},
    {&noto_sans_64, "noto_sans_cjk_64.bin"},
#endif
#endif
};

static const FontMapping BOLD_FONTS[] = {
    {&noto_sans_bold_14, "noto_sans_cjk_bold_14.bin"},
    {&noto_sans_bold_16, "noto_sans_cjk_bold_16.bin"},
    {&noto_sans_bold_18, "noto_sans_cjk_bold_18.bin"},
    {&noto_sans_bold_20, "noto_sans_cjk_bold_20.bin"},
    {&noto_sans_bold_24, "noto_sans_cjk_bold_24.bin"},
    {&noto_sans_bold_28, "noto_sans_cjk_bold_28.bin"},
#if HELIX_MAX_FONT_TIER >= 5
    {&noto_sans_bold_32, "noto_sans_cjk_bold_32.bin"},
#endif
#if HELIX_MAX_FONT_TIER >= 6
    {&noto_sans_bold_40, "noto_sans_cjk_bold_40.bin"},
#if HELIX_HAS_HIDPI_FONTS
    // High-DPI scale rungs; see the noto_sans_48/64 note above.
    {&noto_sans_bold_48, "noto_sans_cjk_bold_48.bin"},
    {&noto_sans_bold_64, "noto_sans_cjk_bold_64.bin"},
#endif
#endif
};

static const FontMapping LIGHT_FONTS[] = {
    {&noto_sans_light_10, "noto_sans_cjk_light_10.bin"},
    {&noto_sans_light_11, "noto_sans_cjk_light_11.bin"},
    {&noto_sans_light_12, "noto_sans_cjk_light_12.bin"},
#if HELIX_MAX_FONT_TIER >= 4
    {&noto_sans_light_14, "noto_sans_cjk_light_14.bin"},
#endif
#if HELIX_MAX_FONT_TIER >= 3
    {&noto_sans_light_16, "noto_sans_cjk_light_16.bin"},
#endif
#if HELIX_MAX_FONT_TIER >= 4
    {&noto_sans_light_18, "noto_sans_cjk_light_18.bin"},
#endif
#if HELIX_MAX_FONT_TIER >= 5
    {&noto_sans_light_20, "noto_sans_cjk_light_20.bin"},
#endif
#if HELIX_MAX_FONT_TIER >= 6
    {&noto_sans_light_26, "noto_sans_cjk_light_26.bin"},
#if HELIX_HAS_HIDPI_FONTS
    // High-DPI scale rungs; see the noto_sans_48/64 note above.
    {&noto_sans_light_32, "noto_sans_cjk_light_32.bin"},
    {&noto_sans_light_40, "noto_sans_cjk_light_40.bin"},
#endif
#endif
};
// clang-format on

static constexpr const char* CJK_FONT_DIR = "A:assets/fonts/cjk/";

CjkFontManager& CjkFontManager::instance() {
    static CjkFontManager s_instance;
    return s_instance;
}

bool CjkFontManager::needs_cjk(const std::string& lang) {
    return lang == "zh" || lang == "ja";
}

void CjkFontManager::on_language_changed(const std::string& lang) {
    if (needs_cjk(lang)) {
        if (!loaded_) {
            if (!load()) {
                spdlog::warn("[CjkFontManager] Failed to load CJK fonts for '{}'", lang);
            }
        }
        current_lang_ = lang;
    } else {
        if (loaded_) {
            unload();
        }
        current_lang_.clear();
    }
}

void CjkFontManager::set_fallback(lv_font_t* compiled, lv_font_t* cjk) {
    compiled->fallback = cjk;
}

void CjkFontManager::clear_fallback(lv_font_t* compiled) {
    compiled->fallback = nullptr;
}

bool CjkFontManager::load() {
    spdlog::info("[CjkFontManager] Loading CJK runtime fonts...");

    auto load_set = [this](const FontMapping* mappings, size_t count) {
        for (size_t i = 0; i < count; ++i) {
            std::string path = std::string(CJK_FONT_DIR) + mappings[i].bin_filename;
            lv_font_t* cjk = lv_binfont_create(path.c_str());
            if (cjk) {
                set_fallback(mappings[i].compiled, cjk);
                loaded_fonts_.push_back({mappings[i].compiled, cjk});
            } else {
                spdlog::warn("[CjkFontManager] Failed to load: {}", path);
            }
        }
    };

    load_set(REGULAR_FONTS, std::size(REGULAR_FONTS));
    load_set(BOLD_FONTS, std::size(BOLD_FONTS));
    load_set(LIGHT_FONTS, std::size(LIGHT_FONTS));

    if (loaded_fonts_.empty()) {
        spdlog::error("[CjkFontManager] No CJK fonts loaded — check assets/fonts/cjk/");
        return false;
    }

    loaded_ = true;
    spdlog::info("[CjkFontManager] Loaded {} CJK fallback fonts", loaded_fonts_.size());
    return true;
}

void CjkFontManager::unload() {
    if (!loaded_)
        return;

    spdlog::info("[CjkFontManager] Unloading {} CJK fonts", loaded_fonts_.size());

    // Clear fallback first so no render call can follow a dangling pointer
    for (auto& entry : loaded_fonts_) {
        clear_fallback(entry.compiled_font);
        lv_binfont_destroy(entry.cjk_font);
    }
    loaded_fonts_.clear();
    loaded_ = false;
    current_lang_.clear();
}

void CjkFontManager::shutdown() {
    unload();
}

} // namespace helix::system
