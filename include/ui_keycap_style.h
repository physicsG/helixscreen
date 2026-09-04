// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file ui_keycap_style.h
 * @brief Depth treatment for on-screen keyboard keys
 *
 * The software keyboard draws its keys as a buttonmatrix, so every key shares
 * one style (LV_PART_ITEMS). This module decides how much physical depth those
 * keys get and derives the colours for it, as pure functions over the theme and
 * the detected hardware tier.
 *
 * Two treatments, picked by hardware:
 *   - SKIRT:  a hard-edged shadow under each key, the keycap rim look. Costs one
 *             box-shadow per key, but at blur width 1 (lv_draw_rect.c only skips
 *             the shadow at width 0), so it is a rim rather than a blur.
 *   - EMBOSS: an edge drawn inside the key face, no shadow at all.
 *             Software shadow blur is LVGL's most expensive primitive and 30+
 *             keys pay it on every matrix repaint, so constrained hardware gets
 *             the border instead of dropping to flat.
 *
 * Colours derive from the key background rather than the theme's cast-shadow
 * black: a keycap rim is the side wall of the key, not a shadow it throws.
 *
 * The theme's shadow_intensity tunes how deep the skirt sits. Zero means
 * "default depth", not "off" — the same reading theme_manager.cpp already gives
 * it for slider knobs, and the reason a stock theme still gets keycaps.
 */

#pragma once

#include "platform_capabilities.h"

#include <lvgl.h>
#include <optional>

namespace helix::ui {

/// How much depth the keyboard's keys render with.
enum class KeycapDepth {
    FLAT,   ///< No depth. Builds where shadow rendering is disabled outright.
    EMBOSS, ///< An edge on the key itself, no shadow. Constrained hardware.
    SKIRT,  ///< Hard-edged shadow under the key. The keycap look.
};

/// Resolved per-key style. Every field is set for every depth so that applying
/// it is idempotent — a re-apply after a theme change clears what it no longer
/// wants rather than leaving the previous treatment behind.
struct KeycapStyle {
    KeycapDepth depth = KeycapDepth::FLAT;

    // SKIRT: zeroed for the other depths.
    int32_t skirt_offset_y = 0; ///< Shadow offset in px; the apparent key height
    lv_color_t skirt_color{};
    lv_opa_t skirt_opa = LV_OPA_TRANSP;

    /// The key's own edge, drawn inside the key face. Where a shadow needs the
    /// background to contrast against, this only needs the key, so it is what
    /// carries the depth on a dark theme (and all of it on EMBOSS).
    int32_t edge_width = 0; ///< Border width in px; 0 = no edge
    lv_color_t edge_color{};
    /// Which sides the edge is drawn on. Light themes shade the underside;
    /// dark themes light the top, which is the only cue that reads when the
    /// keyboard background is darker than the key.
    lv_border_side_t edge_side = LV_BORDER_SIDE_NONE;
    /// Where the edge moves while the key is held, so the light flips sides.
    lv_border_side_t edge_side_pressed = LV_BORDER_SIDE_NONE;

    /// Key background while held. The press cue for every depth, and the only
    /// one EMBOSS and FLAT have.
    lv_color_t pressed_bg{};
};

/// Default skirt depth in px, used when the theme leaves shadow_intensity at 0.
inline constexpr int32_t KEYCAP_DEFAULT_SKIRT_PX = 2;

/// Deepest skirt a theme can ask for. Past this the rim stops reading as a key
/// edge and starts eating the gap between rows.
inline constexpr int32_t KEYCAP_MAX_SKIRT_PX = 3;

/**
 * @brief Pick the depth treatment for this platform.
 *
 * @param tier                 detected hardware tier
 * @param platform_has_shadows false on builds that disable shadow rendering
 *                             outright (ESP32), where even the border is not
 *                             worth the per-key draw op
 * @return the treatment to apply
 */
[[nodiscard]] KeycapDepth decide_keycap_depth(PlatformTier tier, bool platform_has_shadows);

/**
 * @brief Convert the theme's shadow_intensity into a skirt depth in px.
 *
 * shadow_intensity is a 0-50 blur width for cards and dialogs; a keycap rim
 * needs single-digit pixels. Zero (16 of 18 shipped themes) means default
 * depth, so a stock theme still gets keycaps.
 *
 * @param theme_shadow_intensity ThemeProperties::shadow_intensity
 * @return skirt offset in px, always >= 1 (a 0-offset skirt would be invisible)
 */
[[nodiscard]] int32_t keycap_skirt_offset_px(int theme_shadow_intensity);

/**
 * @brief Derive the full key style.
 *
 * @param depth                  treatment from decide_keycap_depth()
 * @param key_bg                 the key's background colour (elevated_bg)
 * @param dark_mode              whether the active theme is in dark mode
 * @param theme_shadow_intensity ThemeProperties::shadow_intensity
 */
[[nodiscard]] KeycapStyle make_keycap_style(KeycapDepth depth, lv_color_t key_bg, bool dark_mode,
                                            int theme_shadow_intensity);

/**
 * @brief Parse a HELIX_KEY_DEPTH override.
 *
 * Lets a slow device force a cheaper treatment than its tier suggests, and lets
 * development check every branch on one machine.
 *
 * @param value environment value; may be nullptr
 * @return the requested depth, or nullopt when unset or unrecognised
 */
[[nodiscard]] std::optional<KeycapDepth> parse_keycap_depth_override(const char* value);

} // namespace helix::ui
