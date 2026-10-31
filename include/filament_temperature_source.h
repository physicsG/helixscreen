// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ams_types.h"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace helix {
class PrinterDiscovery;
class ITransfersAPI;
} // namespace helix

/**
 * @file filament_temperature_source.h
 * @brief Which firmwares publish their own per-filament temperatures, and how
 *        to read them.
 *
 * Every printer has the internal filament DB's generic material table. Some
 * firmwares additionally carry their own per-vendor, per-sub-type temperature
 * table and keep it in JSON files under a Moonraker file root: one flat object
 * per nozzle config, whose keys are spelled {vendor}_{main_type}_{sub_type}_{field}.
 * The files are read over Moonraker's file API, never by asking the firmware to
 * print the table: its console answer lands in every client's console. Those
 * temperatures describe the LOAD and UNLOAD operations, not the printable range:
 * PLA loads at 250°C against a print range nowhere near it, so they are carried
 * as their own fields and never folded into a material's nozzle_min/nozzle_max.
 *
 * This is the one module that knows which firmwares publish the table and the
 * exact shape of its files. Generic code (the active-material resolver, the
 * discovery steps) asks the questions below and never names a firmware.
 * Adding another one is a row in the provider table in
 * filament_temperature_source.cpp.
 */
namespace helix::filament_temps {

/// One leaf of the firmware's table: the temperatures its own load and
/// unload sequences use. Each may be absent in a given leaf.
struct FilamentTemperatures {
    std::optional<int> load_c;
    std::optional<int> unload_c;
};

/// Identity of one table leaf. All three parts are stored lowercased, so
/// lookups compare case-insensitively against slot-reported names.
struct FilamentKey {
    std::string vendor;
    std::string main_type;
    std::string sub_type;

    friend bool operator<(const FilamentKey& a, const FilamentKey& b) {
        if (a.main_type != b.main_type) {
            return a.main_type < b.main_type;
        }
        if (a.vendor != b.vendor) {
            return a.vendor < b.vendor;
        }
        return a.sub_type < b.sub_type;
    }
};

/// Whether this firmware publishes its own per-filament temperature table.
[[nodiscard]] bool firmware_publishes_filament_temperatures(const PrinterDiscovery& hw);

/// The Moonraker file root the table files live under, or an empty string when
/// the printer has no such firmware.
[[nodiscard]] std::string filament_temperature_root(const PrinterDiscovery& hw);

/// The table files, as paths under filament_temperature_root(), in merge order:
/// a leaf present in an earlier file keeps that file's values. Empty when the
/// printer has no such firmware.
[[nodiscard]] std::vector<std::string> filament_temperature_files(const PrinterDiscovery& hw);

/// Parse one table file into the leaves it carries. The file is one flat JSON
/// object keyed {vendor}_{main_type}_{sub_type}_{field}, where main_type
/// carries hyphens and sub_type may carry a space. Only the _load_temp and
/// _unload_temp fields are read; text that does not parse as a JSON object
/// yields an EMPTY map, never a throw.
[[nodiscard]] std::map<FilamentKey, FilamentTemperatures>
parse_filament_temperatures(const std::string& json_text);

/// Fold one parsed file into an accumulating table. The firmware keeps one
/// file per nozzle config listing overlapping materials, so capture keeps the
/// union: a leaf already present keeps its values, and a file lacking a field
/// does not erase what an earlier file supplied.
void merge_filament_temperatures(std::map<FilamentKey, FilamentTemperatures>& table,
                                 const std::map<FilamentKey, FilamentTemperatures>& line);

/// Publish a captured table, replacing any previous one. Called from the
/// transfer thread; safe against concurrent lookups.
void store_filament_temperatures(const std::map<FilamentKey, FilamentTemperatures>& table);

/// Drop the published table (printer gone, or one without the capability
/// discovered in its place).
void clear_filament_temperatures();

/// Resolve the operation temperatures the firmware publishes for one slot.
///
/// The slot's identity reaches the firmware's table with its own spellings:
/// brand is the vendor, material the main type, and spool_name carries the
/// SUB_TYPE product line on the firmwares in this table. Resolution walks
/// the same four-key fallback the firmware's own lookup probes, because the
/// machine's own slots do not spell their names the way the table's keys do:
///
///   1. the slot's vendor (case-insensitive) with the slot's sub-type
///   2. the slot's vendor with sub_type "generic"
///   3. vendor "generic" with the slot's sub-type
///   4. vendor "generic" with sub_type "generic"
///   5. nullopt
///
/// A spool physically loaded in the machine can sit at step 2 — the status
/// may name a vendor/sub-type pair the table has no leaf for. nullopt (also
/// the answer when nothing is published, or when the table was read from a
/// printer REST has since moved away from) means "no firmware opinion", never
/// a temperature of zero.
[[nodiscard]] std::optional<FilamentTemperatures>
lookup_filament_temperatures(const SlotInfo& slot);

/// Connect-time capture: drop any previously published table and, when the
/// printer's firmware keeps one, download every table file and publish the
/// merge of those that parse. A missing or unparseable file is skipped; with
/// none readable nothing is published and callers fall back to the filament
/// DB. Nothing is sent to the printer's console. The table is static firmware
/// data, so once per connection is enough. A reply that lands after REST moved
/// to another printer is dropped.
void capture_on_connect(ITransfersAPI& transfers, const PrinterDiscovery& hw);

} // namespace helix::filament_temps
