// include/chamber_heater_backend.h
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cmath>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "hv/json.hpp"

namespace helix::chamber {

/// Generic fault classification. Vendor codes map to one of these at the
/// backend border; the raw vendor string is kept for logging only and never
/// reaches the UI.
enum class FaultReason { None, Overtemp, SensorFault, CommsLoss, Other };

/// Generic chamber-heater diagnostics — the ONLY shape subjects/UI ever see.
/// Vendor JSON schemas are translated to this at the backend border.
struct ChamberHeaterDiagnostics {
    bool fault = false;
    bool inhibited = false;
    /// Another controller (device web UI, physical button) is currently driving
    /// the heater, not us. Display-only annotation in v1.
    bool externally_controlled = false;
    /// Raw vendor fault code, empty when none. Logs only — the UI binds the
    /// translated chamber_heater_fault_reason_text derived from fault_reason_kind.
    std::string fault_reason;
    FaultReason fault_reason_kind = FaultReason::None; ///< classified kind for UI
    double element_temp_c = NAN;                       ///< heating-element temp; NAN = unknown
    int filter_fan_percent = -1;                       ///< -1 = unknown
    std::string filter_fan_reason;                     ///< empty when none
};

/// One chamber-heater style/brand behind an interface (AMS-backend pattern).
/// Adding a brand = new subclass file + one registry() line. Vendor names live
/// ONLY in chamber_heater_backend_*.cpp.
class ChamberHeaterBackend {
  public:
    virtual ~ChamberHeaterBackend() = default;
    /// Stable backend id (ASCII, logged, never UI-visible). Concrete ids live in the backend .cpp
    /// files.
    virtual std::string_view id() const = 0;
    /// 0 = not mine. Heuristic keyword score for generic; 95 for appliance names.
    virtual int discovery_confidence(const std::string& object_name) const = 0;
    /// Status object carrying diagnostics ("" = this backend has none).
    virtual std::string_view diagnostics_object() const = 0;
    /// Binary filtration-fan output_pin ("" = none).
    virtual std::string_view filter_fan_pin() const = 0;
    /// Gcode clearing a latched fault ("" = none).
    virtual std::string_view fault_reset_gcode() const = 0;
    /// Conservative °C ceiling when configfile max_temp is unknown. 0 = no clamp.
    virtual double conservative_max_temp() const = 0;
    /// Device may self-drive the heater (stock-firmware Auto mode).
    virtual bool device_autonomous_control() const = 0;
    /// Parse this backend's status JSON. nullopt = payload is not mine/unusable.
    virtual std::optional<ChamberHeaterDiagnostics>
    parse_diagnostics(const nlohmann::json& status) const = 0;
};

struct MatchResult {
    const ChamberHeaterBackend* backend = nullptr;
    int confidence = 0;
};

/// Fixed-priority registry. match() picks the highest confidence; ties resolve by registry order
/// (first wins).
const std::vector<const ChamberHeaterBackend*>& registry();

/// Best backend for an object name, or nullptr.
MatchResult match(const std::string& object_name);

/// Lookup by id (nullptr if unknown).
const ChamberHeaterBackend* backend_by_id(std::string_view id);

/// Objects to subscribe for the matched backend's surfaces (diagnostics +
/// filter pin). Inputs come from PrinterDiscovery; empty vector = nothing.
inline std::vector<std::string> required_status_objects(std::string_view diagnostics_object,
                                                        std::string_view filter_fan_pin) {
    std::vector<std::string> out;
    if (!diagnostics_object.empty())
        out.emplace_back(diagnostics_object);
    if (!filter_fan_pin.empty())
        out.emplace_back(filter_fan_pin);
    return out;
}

} // namespace helix::chamber
