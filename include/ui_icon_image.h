// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file ui_icon_image.h
 * @brief Image icons: a PNG's alpha channel drawn by the <icon> widget like an MDI glyph
 *
 * An image icon is registered under a name and drawn wherever that name is an icon
 * `src`. Its glyph is an A8 mask scaled to the icon size, so it takes the icon's
 * variant or color exactly as a font glyph does, and follows theme changes.
 *
 * Every image-icon font lives for the process: a label may hold one after its icon is
 * unregistered. An unregistered or undecodable icon draws its fallback app icon, at the
 * same size, through the font's fallback chain.
 *
 * @threading Main thread only
 */

#pragma once

#include "lvgl/lvgl.h"

#include <cstdint>
#include <string>
#include <vector>

namespace helix::ui::icon {

/// The largest PNG file an image icon reads, and the largest decoded side.
constexpr size_t kMaxImageIconFileBytes = 64 * 1024;
constexpr uint32_t kMaxImageIconSide = 256;

enum class ImageIconStatus {
    Ok,       ///< decoded; the image draws
    Missing,  ///< no such file; the fallback draws
    TooLarge, ///< over kMaxImageIconFileBytes or kMaxImageIconSide; the fallback draws
    Invalid,  ///< not a decodable image; the fallback draws
};

/**
 * Registers @p name as an icon drawn from the alpha channel of the image at @p path (a
 * filesystem path). @p fallback is an app icon name, drawn whenever the image is not
 * available; an unknown one draws the broken-image icon. Registering a name again
 * replaces its image and fallback, and icons already showing it redraw.
 */
ImageIconStatus register_image_icon(const std::string& name, const std::string& path,
                                    const std::string& fallback);

/// Withdraws @p name's image. Icons still showing it draw its fallback.
void unregister_image_icon(const std::string& name);

/// True while @p name is registered and its image draws.
bool image_icon_available(const std::string& name);

/// What the icon widget draws for an image-icon source at one size.
struct ImageIconGlyph {
    const lv_font_t* font = nullptr; ///< the glyph font for this size
    const char* text = nullptr;      ///< the label text it draws (the fallback's codepoint)
};

/**
 * The glyph for @p name at the size of MDI font @p base, or a null font when @p name was
 * never registered. A withdrawn icon still has a glyph: its font then draws the fallback.
 */
ImageIconGlyph image_icon_glyph(const char* name, const lv_font_t* base);

/// The MDI font an image-icon glyph font is sized from, or null when @p font is not one.
const lv_font_t* image_icon_base_font(const lv_font_t* font);

/// The same image icon's glyph font at the size of @p base. @p font must be a glyph font.
const lv_font_t* image_icon_rebase(const lv_font_t* font, const lv_font_t* base);

/**
 * Scales a tight @p src_w x @p src_h alpha plane to fit a @p side x @p side square, aspect
 * kept and centered. Shrinking averages the source pixels each output pixel covers;
 * enlarging repeats the nearest one.
 */
std::vector<uint8_t> scale_alpha_mask(const uint8_t* alpha, uint32_t src_w, uint32_t src_h,
                                      uint32_t side);

} // namespace helix::ui::icon
