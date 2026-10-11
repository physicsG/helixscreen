// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// LVGL-free, so the splash binary's config code can use these without LVGL.
namespace helix::input_defaults {

/// Default scroll momentum decay in percent per indev read (LVGL scroll_throw).
/// ESP32 panels redraw a scrolling list slowly, so a long glide there reads
/// as a stutter rather than momentum; a stronger decay keeps it short.
#if defined(ESP_PLATFORM)
inline constexpr int SCROLL_THROW = 35;
#else
inline constexpr int SCROLL_THROW = 25;
#endif

} // namespace helix::input_defaults
