// SPDX-License-Identifier: GPL-3.0-or-later

#include "filament_temperature_source.h"

#include "i_moonraker_client.h"
#include "text_io.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <mutex>
#include <utility>

#include "hv/json.hpp"

namespace helix::filament_temps {
namespace {

/// One firmware that publishes its own per-filament temperature table.
struct Provider {
    const char* name;          ///< For logs.
    const char* detect_object; ///< Klipper object whose presence identifies it.
    const char* query_gcode;   ///< One-line command that prints the table.
};

// The U1's `filament_parameters` module exists to answer this query; it
// publishes nothing through status, only through the console response.
constexpr Provider PROVIDERS[] = {
    {"Snapmaker U1", "filament_parameters", "FILAMENT_PARA_GET_ALL_INFO"},
};

const Provider* match(const PrinterDiscovery& hw) {
    if (!hw.objects_reported()) {
        return nullptr;
    }
    const auto& objects = hw.printer_objects();
    for (const Provider& p : PROVIDERS) {
        if (std::find(objects.begin(), objects.end(), p.detect_object) != objects.end()) {
            return &p;
        }
    }
    return nullptr;
}

bool ends_with(const std::string& s, const char* suffix) {
    const size_t slen = std::strlen(suffix);
    return s.size() >= slen && s.compare(s.size() - slen, slen, suffix) == 0;
}

/// True when every True/False in the payload sits where the schema puts a
/// boolean literal: after a ':' or '[' (spaces aside) and before a ',', '}' or
/// ']'. A True spelled inside a quoted value fails this and the parse refuses.
bool booleans_are_literals(const std::string& s) {
    for (const char* word : {"True", "False"}) {
        const size_t len = std::strlen(word);
        size_t pos = 0;
        while ((pos = s.find(word, pos)) != std::string::npos) {
            size_t before = pos;
            while (before > 0 && s[before - 1] == ' ') {
                --before;
            }
            if (before == 0 || (s[before - 1] != ':' && s[before - 1] != '[')) {
                return false;
            }
            size_t after = pos + len;
            while (after < s.size() && s[after] == ' ') {
                ++after;
            }
            if (after >= s.size() || (s[after] != ',' && s[after] != '}' && s[after] != ']')) {
                return false;
            }
            pos = after;
        }
    }
    return true;
}

void replace_all(std::string& s, const std::string& from, const std::string& to) {
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
}

/// Read one leaf temperature: an integer the firmware reports, bounded like a
/// temperature. Anything else is absence, never a guess.
std::optional<int> scalar_temp(const nlohmann::json& value) {
    if (!value.is_number_integer()) {
        return std::nullopt;
    }
    const int64_t v = value.get<int64_t>();
    // 0 is the table's unset placeholder: as a load/unload target it would
    // preheat the nozzle to 0C and shadow the DB rung below it.
    if (v <= 0 || v > 999) {
        return std::nullopt;
    }
    return static_cast<int>(v);
}

std::mutex g_table_mutex;
std::map<FilamentKey, FilamentTemperatures> g_table;

} // namespace

bool firmware_publishes_filament_temperatures(const PrinterDiscovery& hw) {
    return match(hw) != nullptr;
}

std::string filament_temperature_query_gcode(const PrinterDiscovery& hw) {
    const Provider* p = match(hw);
    return p ? std::string(p->query_gcode) : std::string();
}

std::map<FilamentKey, FilamentTemperatures>
parse_filament_temperatures(const std::string& response) {
    std::string payload = response;
    const size_t first_char = payload.find_first_not_of(" \t\r\n");
    payload.erase(0, first_char == std::string::npos ? payload.size() : first_char);
    // One console line on the response channel, prefixed "// ".
    if (payload.rfind("// ", 0) == 0) {
        payload.erase(0, 3);
    } else if (payload.rfind("//", 0) == 0) {
        payload.erase(0, 2);
    }

    // The quote/boolean swap below is only sound for the firmware's own
    // spelling; anything richer must refuse rather than corrupt.
    if (payload.find('"') != std::string::npos || payload.find('\\') != std::string::npos ||
        !booleans_are_literals(payload)) {
        return {};
    }
    std::replace(payload.begin(), payload.end(), '\'', '"');
    replace_all(payload, "True", "true");
    replace_all(payload, "False", "false");

    nlohmann::json root = nlohmann::json::parse(payload, nullptr, false);
    if (root.is_discarded()) {
        return {};
    }

    std::map<FilamentKey, FilamentTemperatures> table;
    if (!root.is_object()) {
        return table;
    }
    for (auto field = root.begin(); field != root.end(); ++field) {
        // Keys are {vendor}_{main_type}_{sub_type}_{field}; only load_temp
        // and unload_temp are temperatures. print_temp, flow_k*, vol_speed,
        // is_soft, version, the flow ceilings and the process_* keys are not
        // ours.
        const std::string& key = field.key();
        const bool unload = ends_with(key, "_unload_temp");
        if (!unload && !ends_with(key, "_load_temp")) {
            continue;
        }
        const std::string stem = key.substr(0, key.size() - (unload ? 12 : 10));
        // Split from the left: vendor, main_type, and the REST as sub_type,
        // so a sub_type holding a space or an underscore survives (main_type
        // itself never carries an underscore - the firmware spells it with
        // hyphens). A stem without three parts is skipped, not guessed.
        const size_t first_us = stem.find('_');
        if (first_us == std::string::npos) {
            continue;
        }
        const size_t second_us = stem.find('_', first_us + 1);
        if (second_us == std::string::npos) {
            continue;
        }
        const std::optional<int> temp = scalar_temp(field.value());
        if (!temp) {
            continue;
        }
        FilamentTemperatures& leaf = table[FilamentKey{
            helix::text_io::to_lower(stem.substr(0, first_us)),
            helix::text_io::to_lower(stem.substr(first_us + 1, second_us - first_us - 1)),
            helix::text_io::to_lower(stem.substr(second_us + 1))}];
        if (unload) {
            leaf.unload_c = temp;
        } else {
            leaf.load_c = temp;
        }
    }
    return table;
}

void merge_filament_temperatures(std::map<FilamentKey, FilamentTemperatures>& table,
                                 const std::map<FilamentKey, FilamentTemperatures>& line) {
    for (const auto& entry : line) {
        FilamentTemperatures& leaf = table[entry.first];
        if (!leaf.load_c) {
            leaf.load_c = entry.second.load_c;
        }
        if (!leaf.unload_c) {
            leaf.unload_c = entry.second.unload_c;
        }
    }
}

void store_filament_temperatures(const std::map<FilamentKey, FilamentTemperatures>& table) {
    std::lock_guard<std::mutex> lock(g_table_mutex);
    g_table = table;
}

void clear_filament_temperatures() {
    std::lock_guard<std::mutex> lock(g_table_mutex);
    g_table.clear();
}

std::optional<FilamentTemperatures> lookup_filament_temperatures(const SlotInfo& slot) {
    std::lock_guard<std::mutex> lock(g_table_mutex);
    const std::string main = helix::text_io::to_lower(slot.material);
    const std::string vendor = helix::text_io::to_lower(slot.brand);
    const std::string sub = helix::text_io::to_lower(slot.spool_name);
    auto at = [&](const std::string& v,
                  const std::string& s) -> std::optional<FilamentTemperatures> {
        auto it = g_table.find(FilamentKey{v, main, s});
        if (it == g_table.end()) {
            return std::nullopt;
        }
        return it->second;
    };
    if (auto t = at(vendor, sub)) {
        return t;
    }
    if (auto t = at(vendor, "generic")) {
        return t;
    }
    if (auto t = at("generic", sub)) {
        return t;
    }
    return at("generic", "generic");
}

void capture_on_connect(IMoonrakerClient& client, const PrinterDiscovery& hw) {
    clear_filament_temperatures();
    const Provider* p = match(hw);
    if (!p) {
        return;
    }
    spdlog::info("[FilamentTemps] {} publishes per-filament temperatures; querying", p->name);
    const json params = {{"script", std::string(p->query_gcode)}};
    client.send_jsonrpc("printer.gcode.script", params, [&client](const json& response) {
        if (response.contains("error")) {
            return;
        }
        // Script completion means every line it printed is already in the
        // store. The firmware answers with one dict per nozzle config, each
        // listing a different subset of the materials, so merge every
        // response line instead of keeping a single one.
        client.get_gcode_store(
            32,
            [](const std::vector<GcodeStoreEntry>& entries) {
                std::map<FilamentKey, FilamentTemperatures> table;
                for (auto it = entries.rbegin(); it != entries.rend(); ++it) {
                    if (it->type != "response") {
                        continue;
                    }
                    merge_filament_temperatures(table, parse_filament_temperatures(it->message));
                }
                if (table.empty()) {
                    spdlog::warn(
                        "[FilamentTemps] No temperature table in the gcode store after query");
                    return;
                }
                store_filament_temperatures(table);
                spdlog::info("[FilamentTemps] Published {} per-filament temperature leaves",
                             table.size());
            },
            [](const MoonrakerError&) {});
    });
}

} // namespace helix::filament_temps
