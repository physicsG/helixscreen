// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "accel_sensor_types.h"
#include "sensor_manager.h"

namespace helix::sensors {

/**
 * @brief Accelerometers (ADXL345, LIS2DW, LIS3DH, MPU9250, ICM20948, Beacon's onboard chip).
 *
 * Klipper accelerometers have no get_status(), so they come only from
 * configfile.config sections that PrinterDiscovery classifies; no status frame
 * ever carries them. The base owns discovery, the list and the count subject.
 * Accelerometers have no roles and no persisted settings.
 */
class AccelSensorManager : public SensorManager {
  public:
    static AccelSensorManager& instance();

  protected:
    void register_subjects(SubjectManager& subjects) override;
    void refresh_subjects() override;
    bool parse_status(const nlohmann::json& status) override;
    void on_discovered(const PrinterDiscovery& hw) override;

  private:
    AccelSensorManager();
    static Options make_options();
};

} // namespace helix::sensors
