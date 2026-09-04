// src/printer/chamber_heater_backend_generic.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
// VENDOR_OK: this file is the vendor-knowledge border for chamber heaters.
#include "chamber_heater_backend.h"

#include <algorithm>
#include <cctype>

namespace helix::chamber {
namespace {

std::string to_upper_copy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return s;
}

bool has_standalone_token(const std::string& upper, const char* tok) {
    const std::string token = tok;
    auto is_separator = [](char c) {
        return c == '_' || std::isspace(static_cast<unsigned char>(c));
    };
    size_t i = 0;
    while (i < upper.size()) {
        while (i < upper.size() && is_separator(upper[i])) {
            ++i;
        }
        size_t start = i;
        while (i < upper.size() && !is_separator(upper[i])) {
            ++i;
        }
        if (i - start == token.size() && upper.compare(start, token.size(), token) == 0) {
            return true;
        }
    }
    return false;
}

/// Keyword tiers MOVED VERBATIM from printer_discovery.h chamber_keyword_confidence
/// (which keeps its own copy for sensor/fan paths until Task 3 delegates).
/// CHAMBER 100 > ENCLOSURE 90 > CAVITY 85 > BOX 60; -1 compound, -40 air-quality,
/// floored at 1 when any keyword matched.
int keyword_confidence(const std::string& object_name) {
    std::string upper = to_upper_copy(object_name);
    int score = 0;
    const char* keyword = nullptr;
    if (upper.find("CHAMBER") != std::string::npos) {
        score = 100;
        keyword = "CHAMBER";
    } else if (upper.find("ENCLOSURE") != std::string::npos) {
        score = 90;
        keyword = "ENCLOSURE";
    } else if (upper.find("CAVITY") != std::string::npos) {
        score = 85;
        keyword = "CAVITY";
    } else if (has_standalone_token(upper, "BOX")) {
        score = 60;
        keyword = "BOX";
    } else {
        return 0;
    }
    if (upper != keyword) {
        score -= 1;
    }
    static const char* const AIR_QUALITY_TOKENS[] = {"TVOC",     "VOC",         "CO2",     "GAS",
                                                     "HUMIDITY", "IAQ",         "AQI",     "PM25",
                                                     "PM10",     "PARTICULATE", "PRESSURE"};
    for (const char* tok : AIR_QUALITY_TOKENS) {
        if (has_standalone_token(upper, tok)) {
            score -= 40;
            break;
        }
    }
    return score < 1 ? 1 : score;
}

/// Default backend: any chamber-ish heater_generic / temperature_fan discovered
/// by keyword. No diagnostics, no filter pin, no cap — configfile rules alone.
class GenericChamberHeaterBackend : public ChamberHeaterBackend {
  public:
    std::string_view id() const override {
        return "generic";
    }
    int discovery_confidence(const std::string& object_name) const override {
        return keyword_confidence(object_name);
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
    double conservative_max_temp() const override {
        return 0;
    }
    bool device_autonomous_control() const override {
        return false;
    }
    std::optional<ChamberHeaterDiagnostics>
    parse_diagnostics(const nlohmann::json&) const override {
        return std::nullopt; // no diagnostics surface
    }
};

const GenericChamberHeaterBackend kGeneric;

} // namespace

// Declared at helix::chamber scope (NOT the anonymous namespace) so they link
// against the external definitions in the vendor backend files.
const ChamberHeaterBackend* dragonbreath_backend_instance();
const ChamberHeaterBackend* panda_breath_backend_instance();

const std::vector<const ChamberHeaterBackend*>& registry() {
    static const std::vector<const ChamberHeaterBackend*> REG = {
        &kGeneric,
        dragonbreath_backend_instance(),
        panda_breath_backend_instance(),
    };
    return REG;
}

MatchResult match(const std::string& object_name) {
    MatchResult best;
    for (const auto* backend : registry()) {
        const int conf = backend->discovery_confidence(object_name);
        if (conf > best.confidence) {
            best = {backend, conf};
        }
    }
    return best;
}

const ChamberHeaterBackend* backend_by_id(std::string_view id) {
    for (const auto* backend : registry()) {
        if (backend->id() == id) {
            return backend;
        }
    }
    return nullptr;
}

} // namespace helix::chamber
