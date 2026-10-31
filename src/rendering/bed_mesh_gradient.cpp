// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#if HELIX_HAS_BED_MESH_3D

#include "bed_mesh_gradient.h"

#include "bed_mesh_renderer.h" // For BED_MESH_COLOR_COMPRESSION constant

#include <spdlog/spdlog.h>

#include <algorithm>
#include <mutex>

namespace {

// Color desaturation (for muted heat map appearance)
constexpr double kColorSaturation = 0.65;   // 65% original color
constexpr double kColorDesaturation = 0.35; // 35% grayscale mix

// Heat-map gradient band thresholds (5-band: Purple→Blue→Cyan→Yellow→Red)
constexpr double kGradientBand1End = 0.125; // Purple to Blue transition
constexpr double kGradientBand2End = 0.375; // Blue to Cyan transition
constexpr double kGradientBand3End = 0.625; // Cyan to Yellow transition
constexpr double kGradientBand4End = 0.875; // Yellow to Red transition

// Gradient color RGB values (endpoints of each band)
// Note: Omitted values are 0 (e.g., Purple has G=0, Red has G=0 and B=0)
constexpr uint8_t kGradientPurpleR = 128;
constexpr uint8_t kGradientPurpleB = 255;
constexpr uint8_t kGradientBlueG = 128;
constexpr uint8_t kGradientCyanG = 255;
constexpr uint8_t kGradientYellowR = 255;
constexpr uint8_t kGradientYellowG = 255;
constexpr uint8_t kGradientRedR = 255;

// Color gradient lookup table (pre-computed for performance)
constexpr int kColorGradientLutSize = 1024; // 1024 samples for smooth gradient
lv_color_t g_color_gradient_lut[kColorGradientLutSize];
std::once_flag g_color_gradient_lut_init_flag;

} // anonymous namespace

/**
 * Initialize color gradient lookup table (called once at startup)
 * Pre-computes all gradient colors to avoid repeated calculations
 * Thread-safe via std::call_once
 */
static void init_color_gradient_lut() {
    // Pre-compute gradient for normalized values [0.0, 1.0]
    for (int i = 0; i < kColorGradientLutSize; i++) {
        double normalized = static_cast<double>(i) / (kColorGradientLutSize - 1);

        // Compute RGB using 5-band heat-map (Purple→Blue→Cyan→Yellow→Red)
        uint8_t r, g, b;

        if (normalized < kGradientBand1End) {
            // Band 1: Purple to Blue
            double t = normalized / kGradientBand1End;
            r = static_cast<uint8_t>(kGradientPurpleR * (1.0 - t));
            g = static_cast<uint8_t>(kGradientBlueG * t);
            b = kGradientPurpleB;
        } else if (normalized < kGradientBand2End) {
            // Band 2: Blue to Cyan
            double band_width = kGradientBand2End - kGradientBand1End;
            double t = (normalized - kGradientBand1End) / band_width;
            r = 0;
            g = static_cast<uint8_t>(kGradientBlueG + (kGradientCyanG - kGradientBlueG) * t);
            b = kGradientPurpleB;
        } else if (normalized < kGradientBand3End) {
            // Band 3: Cyan to Yellow
            double band_width = kGradientBand3End - kGradientBand2End;
            double t = (normalized - kGradientBand2End) / band_width;
            r = static_cast<uint8_t>(kGradientYellowR * t);
            g = kGradientCyanG;
            b = static_cast<uint8_t>(kGradientPurpleB * (1.0 - t));
        } else if (normalized < kGradientBand4End) {
            // Band 4: Yellow to Red
            double band_width = kGradientBand4End - kGradientBand3End;
            double t = (normalized - kGradientBand3End) / band_width;
            r = kGradientYellowR;
            g = static_cast<uint8_t>(kGradientYellowG * (1.0 - t));
            b = 0;
        } else {
            // Band 5: Deep Red (maximum temperature)
            r = kGradientRedR;
            g = 0;
            b = 0;
        }

        // Desaturate by 35% for muted appearance
        uint8_t gray = (r + g + b) / 3;
        r = static_cast<uint8_t>(r * kColorSaturation + gray * kColorDesaturation);
        g = static_cast<uint8_t>(g * kColorSaturation + gray * kColorDesaturation);
        b = static_cast<uint8_t>(b * kColorSaturation + gray * kColorDesaturation);

        g_color_gradient_lut[i] = lv_color_make(r, g, b);
    }

    spdlog::debug("[Bed Mesh Gradient] Initialized color gradient LUT with {} samples",
                  kColorGradientLutSize);
}

lv_color_t bed_mesh_gradient_height_to_color(double value, double min_val, double max_val) {
    // Ensure LUT is initialized (thread-safe, runs exactly once)
    std::call_once(g_color_gradient_lut_init_flag, init_color_gradient_lut);

    // Apply color compression for enhanced contrast
    double data_range = max_val - min_val;
    double adjusted_range = data_range * BED_MESH_COLOR_COMPRESSION;
    double data_center = (min_val + max_val) / 2.0;
    double color_min = data_center - (adjusted_range / 2.0);

    // Normalize to [0, 1]
    double normalized = (value - color_min) / adjusted_range;
    normalized = std::max(0.0, std::min(1.0, normalized));

    // Look up color in pre-computed gradient table (10-15% faster than computing)
    // Map normalized [0.0, 1.0] to LUT index [0, 1023]
    int lut_index = static_cast<int>(normalized * (kColorGradientLutSize - 1));
    lut_index = std::max(0, std::min(kColorGradientLutSize - 1, lut_index));

    return g_color_gradient_lut[lut_index];
}

bed_mesh_rgb_t bed_mesh_gradient_lerp_color(bed_mesh_rgb_t a, bed_mesh_rgb_t b, double t) {
    bed_mesh_rgb_t result;
    result.r = static_cast<uint8_t>(a.r + t * (b.r - a.r));
    result.g = static_cast<uint8_t>(a.g + t * (b.g - a.g));
    result.b = static_cast<uint8_t>(a.b + t * (b.b - a.b));
    return result;
}

#endif // HELIX_HAS_BED_MESH_3D
