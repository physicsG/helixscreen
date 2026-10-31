// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "load_cell_types.h"
#include "sensor_manager.h"

#include <map>
#include <string>
#include <vector>

namespace helix::sensors {

/**
 * @brief Load cells (`load_cell`).
 *
 * Discovery, roles and publishing come from SensorManager. This class owns the
 * force readings and pushes the SPOOL_WEIGHT cell's reading to AmsState as the
 * external spool's remaining weight.
 *
 * Status JSON format:
 * @code
 * { "load_cell spool_weight": { "force_g": 45.2 } }
 * @endcode
 */
class LoadCellManager : public SensorManager {
  public:
    static LoadCellManager& instance();

    /// True when an available load cell holds the SPOOL_WEIGHT role.
    [[nodiscard]] bool has_spool_weight_load_cell() const;

    /// Entries with the SPOOL_WEIGHT holder first, then by technical name.
    [[nodiscard]] std::vector<SensorEntry> get_sensors_sorted() const;

    /// Names a load cell the way the rest of the UI does.
    [[nodiscard]] SensorRowView row(size_t index) const override;

  protected:
    void register_subjects(SubjectManager& subjects) override;
    void refresh_subjects() override;
    bool parse_status(const nlohmann::json& status) override;
    void on_discovered(const PrinterDiscovery& hw) override;
    void on_entries_changed() override;

  private:
    friend class LoadCellManagerTestAccess;

    LoadCellManager();
    static Options make_options();

    /// Mutex held: the available SPOOL_WEIGHT entry, or nullptr.
    [[nodiscard]] const SensorEntry* spool_weight_entry_locked() const;

    std::map<std::string, LoadCellState> readings_; ///< by klipper_name
};

} // namespace helix::sensors
