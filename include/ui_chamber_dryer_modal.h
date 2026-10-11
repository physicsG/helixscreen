// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ui_modal.h"

#include "ams_types.h"

#include <string>
#include <vector>

namespace helix::ui {

/// Starts a chamber drying cycle (#1299): a preset from the shared drying
/// presets, clamped to what the chamber's dryer accepts, plus the optional bed
/// assist. Everything is sent through TemperatureController.
class ChamberDryerModal : public Modal {
  public:
    const char* get_name() const override {
        return "Chamber Dryer";
    }
    const char* component_name() const override {
        return "chamber_dryer_modal";
    }

    /// One-shot owned show over the active screen. False when there is no
    /// controller or no dryer to drive.
    static bool show_owned();

    /// "PLA 55°C/4h": the preset as the dryer will run it.
    static std::string preset_label(const DryingPreset& preset, const DryerInfo& dryer);

  protected:
    void on_show() override;
    void on_ok() override;

  private:
    std::vector<DryingPreset> presets_;
};

} // namespace helix::ui
