// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "accel_sensor_manager.h"

#include <algorithm>

namespace helix::sensors {

AccelSensorManager::Options AccelSensorManager::make_options() {
    Options o;
    o.name = "AccelSensorManager";
    o.kinds = {hw::ObjectKind::Accelerometer};
    o.count_subject = "accel_count";
    o.revision_subject = "accel_revision";
    // Accelerometers exist only as config sections.
    o.rediscover_on_config = true;
    o.settings_section = "accel";
    o.row_toggle = RowToggle::None;
    for (const auto& t : kAccelSensorTypes) {
        o.types.push_back({static_cast<uint8_t>(t.value), t.id, t.display});
    }
    return o;
}

AccelSensorManager& AccelSensorManager::instance() {
    static AccelSensorManager instance;
    return instance;
}

AccelSensorManager::AccelSensorManager() : SensorManager(make_options()) {}

void AccelSensorManager::register_subjects(SubjectManager& /*subjects*/) {}

void AccelSensorManager::refresh_subjects() {}

bool AccelSensorManager::parse_status(const nlohmann::json& /*status*/) {
    // No status frame carries an accelerometer.
    return false;
}

void AccelSensorManager::on_discovered(const PrinterDiscovery& /*hw*/) {
    // One chip can be named by two sections (an onboard accelerometer's own section and
    // the resonance tester that references it). The same display name and chip type is
    // one sensor.
    auto& entries = entries_locked();
    std::vector<SensorEntry> unique;
    for (auto& entry : entries) {
        const bool seen = std::any_of(unique.begin(), unique.end(), [&](const SensorEntry& e) {
            return e.display_name == entry.display_name && e.type == entry.type;
        });
        if (!seen) {
            unique.push_back(std::move(entry));
        }
    }
    entries = std::move(unique);
}

} // namespace helix::sensors
