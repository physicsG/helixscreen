// SPDX-License-Identifier: GPL-3.0-or-later

#include "led/led_device_page.h"

#include "led/led_color_utils.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace helix::led {

DevicePage classify_device_page(const LedStripInfo& device, MacroLedType macro_type,
                                bool has_effects) {
    switch (device.backend) {
    case LedBackendType::NATIVE:
        if (device.supports_color) {
            return {LampControl::PowerAndBrightness,
                    device.supports_white ? WhiteMode::WChannel : WhiteMode::Mixed, true,
                    has_effects ? ListKind::Effects : ListKind::None};
        }
        return {LampControl::PowerAndBrightness, WhiteMode::None, false,
                has_effects ? ListKind::Effects : ListKind::LevelChips};
    case LedBackendType::WLED:
        return {LampControl::PowerAndBrightness, WhiteMode::None, false, ListKind::None};
    case LedBackendType::OUTPUT_PIN:
        if (device.is_pwm) {
            return {LampControl::PowerAndBrightness, WhiteMode::None, false, ListKind::LevelChips};
        }
        return {LampControl::PowerOnly, WhiteMode::None, false, ListKind::None};
    case LedBackendType::MACRO:
        switch (macro_type) {
        case MacroLedType::ON_OFF:
            return {LampControl::OnOffButtons, WhiteMode::None, false, ListKind::None};
        case MacroLedType::TOGGLE:
            return {LampControl::ToggleButton, WhiteMode::None, false, ListKind::None};
        case MacroLedType::PRESET:
            return {LampControl::None, WhiteMode::None, false, ListKind::Presets};
        }
        break;
    case LedBackendType::LED_EFFECT:
        break;
    }
    return {};
}

Rgbw white_tone(WhiteTone tone, WhiteMode mode) {
    if (mode == WhiteMode::WChannel) {
        switch (tone) {
        case WhiteTone::Cool:
            return {0.0, 0.0, 0.25, 1.0};
        case WhiteTone::Neutral:
            return {0.0, 0.0, 0.0, 1.0};
        case WhiteTone::Warm:
            return {0.35, 0.12, 0.0, 1.0};
        }
    }
    if (mode == WhiteMode::Mixed) {
        switch (tone) {
        case WhiteTone::Cool:
            return {0.80, 0.88, 1.0, 0.0};
        case WhiteTone::Neutral:
            return {1.0, 0.95, 0.88, 0.0};
        case WhiteTone::Warm:
            return {1.0, 0.72, 0.42, 0.0};
        }
    }
    return {};
}

uint32_t output_rgb(double r, double g, double b, double w) {
    const double m = std::max({r + w, g + w, b + w});
    if (m <= 0.0) {
        return 0xFFFFFF;
    }
    return pack_rgb((r + w) / m, (g + w) / m, (b + w) / m);
}

namespace {

/// Whites run from warm (red high, blue low) to cool (blue high), with green
/// between the two. A white's dimmest channel stays near or above 0.42 of its
/// brightest (a warm white mixed from RGB); Red, the most washed-out default
/// preset whose green does sit between, reaches 0.27.
constexpr double WHITE_MIN_CHANNEL_RATIO = 0.38;
/// Steps a read-back green may stray outside the red-blue span and still count.
constexpr int WHITE_GREEN_SLACK = 6;
/// Per-channel steps of drift a read-back color still counts as the same preset.
constexpr int PRESET_MATCH_TOLERANCE = 12;

int channel(uint32_t rgb, int shift) {
    return static_cast<int>((rgb >> shift) & 0xFF);
}

/// Red over blue: how far a white leans warm (positive) or cool (negative).
double warmth(uint32_t rgb) {
    return (channel(rgb, 16) - channel(rgb, 0)) / 255.0;
}

uint32_t full_scale(uint32_t rgb, double w) {
    double r = 0.0, g = 0.0, b = 0.0;
    unpack_rgb(rgb, r, g, b);
    return output_rgb(r, g, b, w);
}

} // namespace

LookRing ring_for_look(uint32_t rgb, double w, WhiteMode strip_white,
                       const std::vector<uint32_t>& presets) {
    const uint32_t shown = full_scale(rgb, w);
    const int r = channel(shown, 16), g = channel(shown, 8), b = channel(shown, 0);
    const bool neutral = std::min({r, g, b}) >= WHITE_MIN_CHANNEL_RATIO * 255.0 &&
                         g >= std::min(r, b) - WHITE_GREEN_SLACK &&
                         g <= std::max(r, b) + WHITE_GREEN_SLACK;
    LookRing ring;
    if (strip_white != WhiteMode::None && neutral) {
        double best = 1e9;
        for (const WhiteMode mode : {WhiteMode::WChannel, WhiteMode::Mixed}) {
            if (mode == WhiteMode::WChannel && strip_white != WhiteMode::WChannel) {
                continue;
            }
            for (int t = 0; t < 3; ++t) {
                const Rgbw c = white_tone(static_cast<WhiteTone>(t), mode);
                const double d = std::abs(warmth(output_rgb(c.r, c.g, c.b, c.w)) - warmth(shown));
                if (d < best) {
                    best = d;
                    ring.white = t;
                }
            }
        }
        return ring;
    }
    ring.swatch = -2;
    for (size_t i = 0; i < presets.size(); ++i) {
        const uint32_t p = full_scale(presets[i], 0.0);
        int worst = 0;
        for (int shift : {16, 8, 0}) {
            worst = std::max(worst, std::abs(channel(p, shift) - channel(shown, shift)));
        }
        if (worst <= PRESET_MATCH_TOLERANCE) {
            ring.swatch = static_cast<int>(i);
            break;
        }
    }
    return ring;
}

Look fit_look(uint32_t rgb, double w, const LedStripInfo& device) {
    if (!device.supports_color) {
        return device.supports_white ? Look{0, 1.0} : Look{0xFFFFFF, 0.0};
    }
    if (rgb == 0 && w <= 0.0) {
        return {0xFFFFFF, 0.0};
    }
    if (!device.supports_white && w > 0.0) {
        double r = 0.0, g = 0.0, b = 0.0;
        unpack_rgb(rgb, r, g, b);
        return {output_rgb(r, g, b, w), 0.0};
    }
    return {rgb, w};
}

} // namespace helix::led
