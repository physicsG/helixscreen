// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string_view>

/**
 * @file screensaver_registry.h
 * @brief The screensavers the app knows, as data
 *
 * Always compiled, including in builds without screensavers, so settings code can clamp a
 * stored type without the saver sources. The saver implementations are created in
 * src/ui/screensaver_manager.cpp.
 */

/**
 * @brief Available screensaver types
 *
 * Values map directly to the settings dropdown index and persisted config value.
 */
enum class ScreensaverType : int {
    OFF = 0,
    FLYING_TOASTERS = 1,
    STARFIELD = 2,
    PIPES_3D = 3,
    BOUNCING_PRINTER = 4,
    FIREWORKS = 5,
};

namespace helix::ui {

/// Color depths a saver draws at, as bits of ScreensaverInfo::depths.
enum SaverDepth : uint8_t {
    SAVER_DEPTH_16 = 1u << 0,
    SAVER_DEPTH_32 = 1u << 1,
};

/// One registered screensaver.
struct ScreensaverInfo {
    ScreensaverType type;
    /// Stable name: the level store key and a HELIX_SCREENSAVER_NOW value.
    const char* name;
    /// Translation key, which is also the English label in the settings dropdown.
    const char* label_key;
    /// SaverDepth bits the saver draws at.
    uint8_t depths;
};

/// Every screensaver, in type order starting at 1: row i has type i + 1. The settings
/// dropdown in ui_xml/settings_display_overlay.xml lists "Off" and then these labels in
/// this order, which a test checks.
inline constexpr ScreensaverInfo SCREENSAVERS[] = {
    {ScreensaverType::FLYING_TOASTERS, "toasters", "Flying Toasters",
     SAVER_DEPTH_16 | SAVER_DEPTH_32},
    {ScreensaverType::STARFIELD, "starfield", "Starfield", SAVER_DEPTH_16 | SAVER_DEPTH_32},
    {ScreensaverType::PIPES_3D, "pipes", "3D Pipes", SAVER_DEPTH_16 | SAVER_DEPTH_32},
    {ScreensaverType::BOUNCING_PRINTER, "bounce", "Bouncing Printer",
     SAVER_DEPTH_16 | SAVER_DEPTH_32},
    {ScreensaverType::FIREWORKS, "fireworks", "Fireworks", SAVER_DEPTH_16 | SAVER_DEPTH_32},
};

inline constexpr size_t SCREENSAVER_COUNT = std::size(SCREENSAVERS);

/// Translation key and English label of the dropdown's first option.
inline constexpr const char* SCREENSAVER_OFF_LABEL_KEY = "Off";

/// Type a fresh install prefers, when this build can draw it. The screensaver gate measures it
/// and steps it down, or shows a black screen, when it costs too much.
inline constexpr ScreensaverType DEFAULT_SCREENSAVER_TYPE = ScreensaverType::FLYING_TOASTERS;

/// Highest valid type value.
constexpr int screensaver_last_type() {
    return static_cast<int>(SCREENSAVERS[SCREENSAVER_COUNT - 1].type);
}

/// A stored type made valid: below OFF reads as OFF, past the last type as the last type.
constexpr int clamp_screensaver_type(int value) {
    if (value < 0) {
        return 0;
    }
    if (value > screensaver_last_type()) {
        return screensaver_last_type();
    }
    return value;
}

/// The row for `type`, or nullptr for OFF and unregistered values.
constexpr const ScreensaverInfo* find_screensaver(ScreensaverType type) {
    for (const ScreensaverInfo& info : SCREENSAVERS) {
        if (info.type == type) {
            return &info;
        }
    }
    return nullptr;
}

/// Type a fresh install runs at `build_depth` (a SaverDepth bit): the preferred default when it
/// draws at that depth, otherwise the first registered saver that does, and OFF if none do.
/// A 16 bpp build excludes the 32 bpp-only savers, so a default naming one would start nothing.
constexpr ScreensaverType default_screensaver_type(uint8_t build_depth) {
    const ScreensaverInfo* preferred = find_screensaver(DEFAULT_SCREENSAVER_TYPE);
    if (preferred != nullptr && (preferred->depths & build_depth) != 0U) {
        return DEFAULT_SCREENSAVER_TYPE;
    }
    for (const ScreensaverInfo& row : SCREENSAVERS) {
        if ((row.depths & build_depth) != 0U) {
            return row.type;
        }
    }
    return ScreensaverType::OFF;
}

/// The row whose stable name is `name`, or nullptr.
constexpr const ScreensaverInfo* find_screensaver_by_name(std::string_view name) {
    for (const ScreensaverInfo& info : SCREENSAVERS) {
        if (name == info.name) {
            return &info;
        }
    }
    return nullptr;
}

/**
 * @brief Saver a HELIX_SCREENSAVER_NOW value asks for
 *
 * A registered name starts that saver. Any other value starts the configured saver, or
 * flying toasters when the configured type is OFF.
 */
constexpr ScreensaverType resolve_screensaver_now(std::string_view value,
                                                  ScreensaverType configured) {
    if (const ScreensaverInfo* info = find_screensaver_by_name(value)) {
        return info->type;
    }
    return configured != ScreensaverType::OFF ? configured : ScreensaverType::FLYING_TOASTERS;
}

} // namespace helix::ui
