// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ams_backend_mock.h"
#include "ams_types.h"

namespace helix::test {

/// An AMS backend whose one loaded lane names a material and which wants no slot
/// picked for a load, so plan_load() hands Load to the configured macro while the
/// surface still reads lane materials. The material is unknown to the filament
/// database, so its temperature is exactly @c nozzle_c.
class LaneMaterialBackend : public AmsBackendMock {
  public:
    /// @p lane is loaded and names the material. The selected tool maps to
    /// @p selected_slot, which is @p lane unless given. @p lane_tool is the tool
    /// the lane itself maps to (SlotInfo::mapped_tool), none unless given.
    LaneMaterialBackend(int lane, int nozzle_c, int selected_slot = -1, int lane_tool = -1)
        : AmsBackendMock(4), lane_(lane), nozzle_c_(nozzle_c),
          selected_slot_(selected_slot < 0 ? lane : selected_slot), lane_tool_(lane_tool) {}

    [[nodiscard]] AmsSystemInfo get_system_info() const override {
        AmsSystemInfo sys;
        sys.total_slots = 4;
        sys.current_slot = lane_;
        sys.tool_to_slot_map = {selected_slot_};
        return sys;
    }

    [[nodiscard]] SlotInfo get_slot_info(int slot) const override {
        SlotInfo info;
        info.slot_index = slot;
        info.global_index = slot;
        if (slot == lane_) {
            info.material = "Lane Test Filament";
            info.nozzle_temp_min = nozzle_c_;
            info.nozzle_temp_max = nozzle_c_;
            info.mapped_tool = lane_tool_;
        }
        return info;
    }

    [[nodiscard]] int get_current_slot() const override {
        return lane_;
    }

    [[nodiscard]] bool requires_slot_selection_for_load() const override {
        return false;
    }

    [[nodiscard]] bool is_bypass_active() const override {
        return false;
    }

  private:
    int lane_;
    int nozzle_c_;
    int selected_slot_;
    int lane_tool_;
};

} // namespace helix::test
