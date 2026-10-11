// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "led/led_backend.h"

#include <cstdint>
#include <string>
#include <vector>

namespace helix::led {

/// Light-button config value meaning "every switchable device".
constexpr const char* LIGHT_BUTTON_ALL = "all";

constexpr const char* LIGHT_BUTTON_PENDING_PATH = "leds/light_button_pending";
constexpr const char* AUTO_STATE_STRIPS_PATH = "leds/auto_state/strips";

/// Where a saved leds/selected_strips list goes when it stops being user-facing.
struct SelectionMigration {
    std::string light_button; ///< device id, LIGHT_BUTTON_ALL, or "" for the chamber light
    std::vector<std::string> auto_state_strips;

    bool operator==(const SelectionMigration& o) const {
        return light_button == o.light_button && auto_state_strips == o.auto_state_strips;
    }
};

/// One selected id: that device. Every @p switchable id selected (extra ids that
/// are not switchable yet do not count against it): LIGHT_BUTTON_ALL. Anything
/// else leaves the light button on the chamber light. Auto-state keeps @p selected.
SelectionMigration plan_selection_migration(const std::vector<std::string>& selected,
                                            const std::vector<std::string>& switchable);

/// red, orange, green, cyan, blue, purple, pink
constexpr uint32_t DEFAULT_COLOR_PRESETS[] = {0xFF4444, 0xFF6B35, 0x66BB6A, 0x00BCD4,
                                              0x2962FF, 0x9C27B0, 0xFF4081};

/// Recognised on load so that a list nobody edited becomes DEFAULT_COLOR_PRESETS.
constexpr uint32_t PRE_1_1_DEFAULT_COLOR_PRESETS[] = {0xFFFFFF, 0xFFD700, 0xFF6B35, 0x4FC3F7,
                                                      0xFF4444, 0x66BB6A, 0x9C27B0, 0x00BCD4};

/// @p saved, unless it is empty or the untouched pre-1.1 default.
std::vector<uint32_t> migrate_color_presets(const std::vector<uint32_t>& saved);

/// The printer's main light: the first NATIVE or OUTPUT_PIN device whose Klipper
/// object name (the id after its type prefix) is chamber_light, chamber_LED,
/// case_light or caselight, compared case-insensitively and preferred in that
/// order. @p fallback when none matches.
std::string resolve_chamber_light(const std::vector<LedStripInfo>& devices,
                                  const std::string& fallback);

/// Device ids a light button whose `led` config is @p key drives. Empty key: the
/// chamber light. LIGHT_BUTTON_ALL: every switchable device. A switchable id: that
/// device. An id that no longer exists: the chamber light.
std::vector<std::string> resolve_light_targets(const std::string& key,
                                               const std::vector<std::string>& switchable,
                                               const std::string& chamber);

/// Toggles @p id in @p current: removed if present, unless it is the only entry
/// (the "Applies to" chip row never lets the last chip be deselected); appended
/// otherwise.
std::vector<std::string> toggle_target(const std::vector<std::string>& current,
                                       const std::string& id);

/// resolve_light_targets() over every key, deduplicated in first-seen order. No
/// keys at all means the chamber light.
std::vector<std::string> union_light_targets(const std::vector<std::string>& keys,
                                             const std::vector<std::string>& switchable,
                                             const std::string& chamber);

/// Where the LEDs overlay opens: @p requested, else @p last_focused, else
/// @p chamber, whichever is first among @p devices; else the first device.
/// Empty when there are no devices.
std::string pick_overlay_focus(const std::string& requested, const std::string& last_focused,
                               const std::string& chamber, const std::vector<std::string>& devices);

/// The one name every LED surface shows for @p device: a macro device's
/// configured name, otherwise its Klipper or WLED object name prettified
/// ("neopixel chamber_light" -> "Chamber Light").
std::string device_display_name(const LedStripInfo& device);

/// The one-line summary of what a macro device runs, as Settings and the LEDs
/// overlay show it.
std::string macro_device_note(const LedMacroInfo& macro);

enum class PowerState : int { Off = 0, On = 1, Unknown = 2 };

/// What the UI can say about one device right now.
struct DeviceState {
    PowerState power = PowerState::Unknown;
    int brightness = 0;      ///< 0-100
    uint32_t rgb = 0xFFFFFF; ///< full-brightness hue; meaningful only when has_rgb
    bool has_rgb = false;    ///< false: draw the theme's light color instead
};

/// What a light button sends when tapped: off while any target is on, on while
/// any is off, and with nothing readable the opposite of what it last sent.
bool next_power_on(const std::vector<PowerState>& states, bool last_sent_on);

} // namespace helix::led
