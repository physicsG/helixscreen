// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "printer_image_variants.h"

#include "json_utils.h"

#include <algorithm>

namespace helix::printer_images {

namespace {

constexpr std::string_view kFilamentSystem = "filament_system";
constexpr std::string_view kEnclosureTop = "enclosure_top";
constexpr std::string_view kChamberHeater = "chamber_heater";

/// A boolean condition against @p fact; any other value never holds.
bool bool_condition_holds(const nlohmann::json& value, bool fact) {
    return value.is_boolean() && value.get<bool>() == fact;
}

/// The AmsType a `filament_system` value names, NONE when it names none.
AmsType parse_filament_system(const nlohmann::json& value) {
    if (!value.is_string()) {
        return AmsType::NONE;
    }
    const AmsType type = ams_type_from_string(value.get_ref<const std::string&>());
    return is_filament_system(type) ? type : AmsType::NONE;
}

bool condition_holds(std::string_view key, const nlohmann::json& value,
                     const HardwareFacts& facts) {
    if (key == kFilamentSystem) {
        const AmsType wanted = parse_filament_system(value);
        if (wanted == AmsType::NONE) {
            return false;
        }
        return std::find(facts.filament_systems.begin(), facts.filament_systems.end(), wanted) !=
               facts.filament_systems.end();
    }
    if (key == kEnclosureTop) {
        return bool_condition_holds(value, facts.enclosure_top);
    }
    if (key == kChamberHeater) {
        return bool_condition_holds(value, facts.has_chamber_heater);
    }
    return false;
}

} // namespace

bool is_known_variant_condition(std::string_view key) {
    return key == kFilamentSystem || key == kEnclosureTop || key == kChamberHeater;
}

bool is_valid_variant_condition_value(std::string_view key, const nlohmann::json& value) {
    if (key == kFilamentSystem) {
        return parse_filament_system(value) != AmsType::NONE;
    }
    if (key == kEnclosureTop || key == kChamberHeater) {
        return value.is_boolean();
    }
    return false;
}

bool variant_matches(const nlohmann::json& when, const HardwareFacts& facts) {
    if (!when.is_object()) {
        return false;
    }
    for (auto it = when.begin(); it != when.end(); ++it) {
        if (!condition_holds(it.key(), it.value(), facts)) {
            return false;
        }
    }
    return true;
}

std::string select_image(const nlohmann::json& entry, const HardwareFacts& facts) {
    if (const auto* variants = json_util::find_member(entry, "image_variants");
        variants && variants->is_array()) {
        for (const auto& variant : *variants) {
            const auto* when = json_util::find_member(variant, "when");
            const std::string image = json_util::safe_string(variant, "image");
            if (when && !image.empty() && variant_matches(*when, facts)) {
                return image;
            }
        }
    }
    return json_util::safe_string(entry, "image");
}

} // namespace helix::printer_images
