// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "led/led_backend.h"

#include <cstdint>
#include <vector>

namespace helix::led {

/// What the left (lamp) column of an LED device page holds.
enum class LampControl : int {
    PowerAndBrightness = 0,
    PowerOnly = 1,
    OnOffButtons = 2,
    ToggleButton = 3,
    None = 4,
};

/// How the White section reaches white, or that there is none.
enum class WhiteMode : int { None = 0, WChannel = 1, Mixed = 2 };

/// The chip row under the right column.
enum class ListKind : int { None = 0, Effects = 1, Presets = 2, LevelChips = 3 };

/// The sections one device's page shows. Values double as the page subjects'
/// integers, so the XML binds to them directly.
struct DevicePage {
    LampControl lamp = LampControl::None;
    WhiteMode white = WhiteMode::None;
    bool color = false;
    ListKind list = ListKind::None;

    bool operator==(const DevicePage& o) const {
        return lamp == o.lamp && white == o.white && color == o.color && list == o.list;
    }
};

/// The page for @p device. @p macro_type is read only for MACRO devices;
/// @p has_effects says whether led_effect defines any effect for this strip.
DevicePage classify_device_page(const LedStripInfo& device, MacroLedType macro_type,
                                bool has_effects);

enum class WhiteTone : int { Cool = 0, Neutral = 1, Warm = 2 };

/// Channel levels 0.0-1.0 at full brightness.
struct Rgbw {
    double r = 0.0, g = 0.0, b = 0.0, w = 0.0;

    bool operator==(const Rgbw& o) const {
        return r == o.r && g == o.g && b == o.b && w == o.w;
    }
};

/// The fixed white tones. RGBW strips light the W channel and tint it with RGB;
/// RGB strips mix white from RGB alone.
Rgbw white_tone(WhiteTone tone, WhiteMode mode);

constexpr int LEVEL_CHIPS[] = {10, 25, 50, 75, 100};

/// A full-brightness look: an RGB tint (0xRRGGBB) plus a W level 0.0-1.0.
struct Look {
    uint32_t rgb = 0xFFFFFF;
    double w = 0.0;

    bool operator==(const Look& o) const {
        return rgb == o.rgb && w == o.w;
    }
};

/// @p rgb and @p w as @p device can show them. No color: brightness only, all
/// on W when the strip has one, else full on every RGB pin. Color without W:
/// W folds into RGB. Nothing lit at all: white.
Look fit_look(uint32_t rgb, double w, const LedStripInfo& device);

/// Which White tone and which swatch a look rings. white: a WhiteTone or -1.
/// swatch: a preset index, -2 for Custom, -1 when the look is a white.
struct LookRing {
    int white = -1;
    int swatch = -1;
};

/// The ring for a full-brightness look (@p rgb tint plus @p w) on a strip whose
/// White section is @p strip_white. Both sides of every comparison are scaled so
/// their brightest channel is full. A near-neutral look (channels agree after
/// scaling, or W alone) is a white and rings the tone nearest its warmth, never
/// Custom; otherwise the matching preset rings, else Custom.
LookRing ring_for_look(uint32_t rgb, double w, WhiteMode strip_white,
                       const std::vector<uint32_t>& presets);

/// What an RGB(W) output looks like at full brightness: W adds to every
/// channel, then the result scales so its brightest channel is full. White
/// when nothing is lit.
uint32_t output_rgb(double r, double g, double b, double w);

} // namespace helix::led
