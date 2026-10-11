// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <spdlog/fmt/fmt.h>

#include <cmath>
#include <string>

namespace helix {

/// Generate the gcode command to set a fan's speed.
/// @param fan Full Moonraker object name (e.g., "fan", "output_pin fan0", "fan_generic aux")
/// @param speed_percent 0.0-100.0
/// @return Gcode string ready for execute_gcode()
inline std::string fan_gcode(const std::string& fan, double speed_percent) {
    int s_value = static_cast<int>(std::round(speed_percent * 255.0 / 100.0));

    // Canonical Klipper [fan] section
    if (fan == "fan") {
        if (s_value == 0) {
            return "M107";
        }
        return "M106 S" + std::to_string(s_value);
    }

    // output_pin fan objects (Creality-style)
    if (fan.rfind("output_pin ", 0) == 0) {
        std::string short_name = fan.substr(11);

        // output_pin fanN -> M106 P<N> S<value>
        if (short_name.rfind("fan", 0) == 0 && short_name.size() > 3) {
            std::string index_str = short_name.substr(3);
            // Verify it's a number
            bool is_numeric = !index_str.empty();
            for (char c : index_str) {
                if (!std::isdigit(c)) {
                    is_numeric = false;
                    break;
                }
            }
            if (is_numeric) {
                if (s_value == 0) {
                    return "M107 P" + index_str;
                }
                return "M106 P" + index_str + " S" + std::to_string(s_value);
            }
        }

        // Non-numeric output_pin fan or non-fan output_pin: SET_PIN
        return fmt::format("SET_PIN PIN={} VALUE={:.2f}", short_name, speed_percent / 100.0);
    }

    // All other fan types: SET_FAN_SPEED
    std::string fan_name = fan;
    size_t space_pos = fan_name.find(' ');
    if (space_pos != std::string::npos) {
        fan_name = fan_name.substr(space_pos + 1);
    }
    return fmt::format("SET_FAN_SPEED FAN={} SPEED={:.2f}", fan_name, speed_percent / 100.0);
}

} // namespace helix
