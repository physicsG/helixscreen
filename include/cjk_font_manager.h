// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "lvgl/lvgl.h"

#include <string>
#include <vector>

namespace helix::system {

class CjkFontManager {
  public:
    static CjkFontManager& instance();

    void on_language_changed(const std::string& lang);

    bool is_loaded() const {
        return loaded_;
    }

    /// Number of compiled faces currently carrying a CJK fallback. Compared
    /// against the count of baked .bin files on disk, this proves every baked
    /// fallback is reachable through the mapping tables and every mapped
    /// entry resolves to a real file.
    size_t loaded_font_count() const {
        return loaded_fonts_.size();
    }

    void shutdown();

  private:
    CjkFontManager() = default;

    static bool needs_cjk(const std::string& lang);
    bool load();
    void unload();

    static void set_fallback(lv_font_t* compiled, lv_font_t* cjk);
    static void clear_fallback(lv_font_t* compiled);

    struct FontEntry {
        lv_font_t* compiled_font;
        lv_font_t* cjk_font;
    };

    std::vector<FontEntry> loaded_fonts_;
    std::string current_lang_;
    bool loaded_ = false;
};

} // namespace helix::system
