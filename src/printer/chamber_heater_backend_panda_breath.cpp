// src/printer/chamber_heater_backend_panda_breath.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
// VENDOR_OK: stock Panda Breath firmware knowledge lives here and nowhere else.
// Schema NOT hardware-verified: heater + ceiling only (see spec §Backends).
#include "chamber_heater_backend.h"

#include <cctype>

namespace helix::chamber {
namespace {

class PandaBreathBackend : public ChamberHeaterBackend {
  public:
    std::string_view id() const override {
        return "panda_breath";
    }

    int discovery_confidence(const std::string& object_name) const override {
        std::string lower;
        for (char c : object_name) {
            lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
        if (lower.find("panda_breath") != std::string::npos ||
            lower.find("pandabreath") != std::string::npos) {
            return 95;
        }
        return 0;
    }

    std::string_view diagnostics_object() const override {
        return {};
    }
    std::string_view filter_fan_pin() const override {
        return {};
    }
    std::string_view fault_reset_gcode() const override {
        return {};
    }
    // Stock firmware caps the chamber at 60 C.
    double conservative_max_temp() const override {
        return 60.0;
    }
    // Stock Auto mode drives the heater from bed temperature on-device.
    bool device_autonomous_control() const override {
        return true;
    }

    std::optional<ChamberHeaterDiagnostics>
    parse_diagnostics(const nlohmann::json&) const override {
        return std::nullopt; // no verified status surface yet
    }
};

const PandaBreathBackend kPandaBreath;

} // namespace

const ChamberHeaterBackend* panda_breath_backend_instance() {
    return &kPandaBreath;
}

} // namespace helix::chamber
