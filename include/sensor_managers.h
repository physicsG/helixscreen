// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "accel_sensor_manager.h"
#include "filament_sensor_manager.h"
#include "humidity_sensor_manager.h"
#include "load_cell_manager.h"
#include "probe_sensor_manager.h"
#include "temperature_sensor_manager.h"
#include "width_sensor_manager.h"

namespace helix::sensors {

/// Calls fn on every Klipper sensor manager singleton. The one list of them:
/// subject init and the status fan-out both walk it, so a manager added here
/// gets both. Discovery is per manager, because each reads a different input.
template <typename Fn> void for_each_sensor_manager(Fn&& fn) {
    fn(FilamentSensorManager::instance());
    fn(HumiditySensorManager::instance());
    fn(WidthSensorManager::instance());
    fn(ProbeSensorManager::instance());
    fn(AccelSensorManager::instance());
    fn(TemperatureSensorManager::instance());
    fn(LoadCellManager::instance());
}

} // namespace helix::sensors
