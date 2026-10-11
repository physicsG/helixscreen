// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_filament_temperature_source.cpp
 * @brief The firmware-published per-filament operation temperatures.
 *
 * Run with: ./build/bin/helix-tests "[snapmaker][filament-temps]"
 *
 * The fixture below is the U1's FILAMENT_PARA_GET_ALL_INFO answer, captured
 * verbatim from the machine: one console line on the response channel per
 * nozzle config, each a flat Python dict literal whose keys are spelled
 * {vendor}_{main_type}_{sub_type}_{field}. One invocation prints five dicts
 * listing overlapping subsets of the materials, so capture merges them into
 * the union. Every dict also carries keys the temperature table does not own
 * (print_temp, flow_k*, vol_speed, is_soft, version, the flow ceilings,
 * process_*), which the parser must skip.
 */
#include "active_material_provider.h"
#include "ams_types.h"
#include "filament_database.h"
#include "filament_op_slot_resolver.h"
#include "filament_temperature_source.h"
#include "material_settings_manager.h"
#include "printer_discovery.h"

#include <map>

#include "../catch_amalgamated.hpp"

using namespace helix;
using filament_temps::FilamentKey;
using filament_temps::FilamentTemperatures;

namespace {

/// One complete response line from the real printer, captured verbatim:
/// seven materials, all vendor generic, hyphenated main types (PETG-CF,
/// PA6-CF) and one space-bearing sub type (TPU 95A HF).
const char* REAL_RESPONSE_LINE =
    "// {'version': '1.0.0', 'hard_filaments_max_flow_k': 0.4, 'soft_filaments_max_flow_k': 0.6, "
    "'generic_PLA_generic_load_temp': 250, 'generic_PLA_generic_unload_temp': 250, "
    "'generic_PETG_generic_load_temp': 270, 'generic_PETG_generic_unload_temp': 270, "
    "'generic_ABS_generic_load_temp': 280, 'generic_ABS_generic_unload_temp': 280, "
    "'generic_ASA_generic_load_temp': 280, 'generic_ASA_generic_unload_temp': 280, "
    "'generic_PETG-CF_generic_load_temp': 270, 'generic_PETG-CF_generic_unload_temp': 270, "
    "'generic_TPU_95A HF_load_temp': 250, 'generic_TPU_95A HF_unload_temp': 250, "
    "'generic_PA6-CF_generic_load_temp': 290, 'generic_PA6-CF_generic_unload_temp': 290}";

/// Wipes both the published firmware table and any staged user override, so a
/// case never reads what a previous case stored. Same pattern as
/// test_active_material_provider.cpp's OverrideFixture.
struct FilamentTempsFixture {
    FilamentTempsFixture() {
        filament_temps::clear_filament_temperatures();
    }
    ~FilamentTempsFixture() {
        filament_temps::clear_filament_temperatures();
        auto& mgr = MaterialSettingsManager::instance();
        for (const char* name : {"PETG", "PLA"}) {
            if (mgr.has_override(name)) {
                mgr.clear_override(name);
            }
        }
    }

    void store_captured_table() const {
        auto table = filament_temps::parse_filament_temperatures(REAL_RESPONSE_LINE);
        REQUIRE(!table.empty());
        filament_temps::store_filament_temperatures(table);
    }
};

SlotInfo slot(const char* brand, const char* material, const char* sub_type) {
    SlotInfo s;
    s.brand = brand;
    s.material = material;
    s.spool_name = sub_type; // carries SUB_TYPE on the firmwares in the table
    return s;
}

} // namespace

// ============================================================================
// parse_filament_temperatures()
// ============================================================================

TEST_CASE("filament-temps parse: the captured response yields the table",
          "[snapmaker][filament-temps]") {
    auto table = filament_temps::parse_filament_temperatures(REAL_RESPONSE_LINE);

    // Seven materials; version and the two flow ceilings ride along in the
    // same dict and must not become leaves.
    REQUIRE(table.size() == 7);
    CHECK(table.find(FilamentKey{"generic", "pla", "generic"}) != table.end());
    CHECK(table.find(FilamentKey{"generic", "petg", "generic"}) != table.end());
    CHECK(table.find(FilamentKey{"generic", "abs", "generic"}) != table.end());
    CHECK(table.find(FilamentKey{"generic", "asa", "generic"}) != table.end());
    CHECK(table.find(FilamentKey{"generic", "petg-cf", "generic"}) != table.end());
    CHECK(table.find(FilamentKey{"generic", "tpu", "95a hf"}) != table.end());
    CHECK(table.find(FilamentKey{"generic", "pa6-cf", "generic"}) != table.end());
}

TEST_CASE("filament-temps parse: hyphenated main types and spaced sub types round-trip",
          "[snapmaker][filament-temps]") {
    auto table = filament_temps::parse_filament_temperatures(REAL_RESPONSE_LINE);

    auto pa6 = table.find(FilamentKey{"generic", "pa6-cf", "generic"});
    REQUIRE(pa6 != table.end());
    CHECK(pa6->second.load_c.value() == 290);
    CHECK(pa6->second.unload_c.value() == 290);

    auto tpu = table.find(FilamentKey{"generic", "tpu", "95a hf"});
    REQUIRE(tpu != table.end());
    CHECK(tpu->second.load_c.value() == 250);
    CHECK(tpu->second.unload_c.value() == 250);
}

TEST_CASE("filament-temps parse: only the two temperature suffixes are ours",
          "[snapmaker][filament-temps]") {
    auto table = filament_temps::parse_filament_temperatures(
        "// {'generic_PLA_Silk_load_temp': 240, 'Snapmaker_PLA_Basic_print_temp': 225, "
        "'generic_PLA_Silk_vol_speed': 42, 'generic_PLA_Silk_is_soft': False, "
        "'generic_PLA_Silk_flow_k': 0.02, 'generic_PLA_load_temp': 999, "
        "'process_print_accel': 5000}");
    // print_temp / vol_speed / flow_k / process_* keys are skipped, and the
    // two-part stem "generic_PLA" has no sub_type to split out, so it is
    // skipped too rather than guessed into a leaf.
    REQUIRE(table.size() == 1);
    auto it = table.find(FilamentKey{"generic", "pla", "silk"});
    REQUIRE(it != table.end());
    CHECK(it->second.load_c.value() == 240);
    CHECK_FALSE(it->second.unload_c.has_value());
}

TEST_CASE("filament-temps parse: a zero temperature is an unset placeholder, not a target",
          "[snapmaker][filament-temps]") {
    // A 0 here would win over the DB rung and preheat the nozzle to 0C.
    auto table = filament_temps::parse_filament_temperatures(
        "// {'generic_PLA_Silk_load_temp': 0, 'generic_PLA_Silk_unload_temp': 230, "
        "'generic_PETG_HF_load_temp': -5}");
    auto silk = table.find(FilamentKey{"generic", "pla", "silk"});
    REQUIRE(silk != table.end());
    CHECK_FALSE(silk->second.load_c.has_value());
    CHECK(silk->second.unload_c.value() == 230);
    CHECK(table.find(FilamentKey{"generic", "petg", "hf"}) == table.end());
}

TEST_CASE("filament-temps parse: banner and malformed lines yield an empty map, never a throw",
          "[snapmaker][filament-temps]") {
    // FILAMENT_PARA_GET_ALL_INFO prints this banner before the dicts.
    CHECK(filament_temps::parse_filament_temperatures(
              "// [filament_parameters] get all filament parameters")
              .empty());
    // Truncated mid-dict, as a dropped websocket frame would leave it.
    CHECK(filament_temps::parse_filament_temperatures(
              "// {'version': '1.0.0', 'generic_PLA_generic_load_temp': 25")
              .empty());
    // A JSON body: the normalisation only accepts the firmware's own quoting,
    // and says so by refusing rather than corrupting.
    CHECK(
        filament_temps::parse_filament_temperatures(R"(// {"generic_PLA_generic_load_temp": 250})")
            .empty());
    CHECK(filament_temps::parse_filament_temperatures("// ok").empty());
    CHECK(filament_temps::parse_filament_temperatures("").empty());
}

// ============================================================================
// merge_filament_temperatures() — one dict per nozzle config, union is the table
// ============================================================================

TEST_CASE("filament-temps merge: two response lines yield the union",
          "[snapmaker][filament-temps]") {
    auto table = filament_temps::parse_filament_temperatures(REAL_RESPONSE_LINE);
    REQUIRE(table.size() == 7);

    // A second nozzle config's dict: PLA again (load only this time) plus a
    // material the first line does not carry.
    auto line = filament_temps::parse_filament_temperatures(
        "// {'generic_PLA_generic_load_temp': 250, 'generic_PETG-HF_generic_load_temp': 260, "
        "'generic_PETG-HF_generic_unload_temp': 260}");
    REQUIRE(line.size() == 2);

    filament_temps::merge_filament_temperatures(table, line);
    REQUIRE(table.size() == 8);

    auto hf = table.find(FilamentKey{"generic", "petg-hf", "generic"});
    REQUIRE(hf != table.end());
    CHECK(hf->second.load_c.value() == 260);
    CHECK(hf->second.unload_c.value() == 260);

    // A leaf both lines list keeps its values: the second line carries no
    // unload_temp, and absence does not erase what the first line supplied.
    auto pla = table.find(FilamentKey{"generic", "pla", "generic"});
    REQUIRE(pla != table.end());
    CHECK(pla->second.load_c.value() == 250);
    CHECK(pla->second.unload_c.value() == 250);
}

// ============================================================================
// lookup_filament_temperatures() — the firmware's four-key fallback chain
// ============================================================================

TEST_CASE("filament-temps lookup: exact vendor/type/sub resolves directly",
          "[snapmaker][filament-temps]") {
    FilamentTempsFixture fixture;
    fixture.store_captured_table();
    auto t = filament_temps::lookup_filament_temperatures(slot("Generic", "TPU", "95A HF"));
    REQUIRE(t.has_value());
    CHECK(t->load_c.value() == 250);
    CHECK(t->unload_c.value() == 250);
}

TEST_CASE("filament-temps lookup: a sub the table lacks falls back to sub generic",
          "[snapmaker][filament-temps]") {
    // The machine's own slots spell product-line names the table has no leaf
    // for; vendor generic with sub generic still answers for a physically
    // loaded spool.
    FilamentTempsFixture fixture;
    fixture.store_captured_table();
    auto t = filament_temps::lookup_filament_temperatures(slot("Generic", "PETG", "SnapSpeed"));
    REQUIRE(t.has_value());
    CHECK(t->load_c.value() == 270);
    CHECK(t->unload_c.value() == 270);
}

TEST_CASE("filament-temps lookup: an unknown vendor falls back to vendor generic with the sub",
          "[snapmaker][filament-temps]") {
    FilamentTempsFixture fixture;
    fixture.store_captured_table();
    // The table has no vendor "BrandX" and no tpu/generic leaf, so only the
    // (generic, sub) rung can answer this one.
    auto t = filament_temps::lookup_filament_temperatures(slot("BrandX", "TPU", "95A HF"));
    REQUIRE(t.has_value());
    CHECK(t->load_c.value() == 250);
    CHECK(t->unload_c.value() == 250);
}

TEST_CASE("filament-temps lookup: the generic/sub rung fires before generic/generic",
          "[snapmaker][filament-temps]") {
    FilamentTempsFixture fixture;
    auto table = filament_temps::parse_filament_temperatures(
        "// {'generic_PLA_Silk_load_temp': 240, 'generic_PLA_generic_load_temp': 220}");
    REQUIRE(table.size() == 2);
    filament_temps::store_filament_temperatures(table);

    // (generic, silk) answers 240; rung 4 would answer 220, so this pins the
    // firmware's order between the last two rungs.
    auto t = filament_temps::lookup_filament_temperatures(slot("BrandX", "PLA", "Silk"));
    REQUIRE(t.has_value());
    CHECK(t->load_c.value() == 240);

    // Nothing matches the sub, so the final (generic, generic) rung answers.
    t = filament_temps::lookup_filament_temperatures(slot("BrandX", "PLA", "Junk"));
    REQUIRE(t.has_value());
    CHECK(t->load_c.value() == 220);
}

TEST_CASE("filament-temps lookup: a type the table does not carry is nullopt",
          "[snapmaker][filament-temps]") {
    FilamentTempsFixture fixture;
    fixture.store_captured_table();
    CHECK_FALSE(
        filament_temps::lookup_filament_temperatures(slot("Generic", "NITINOL", "")).has_value());
}

TEST_CASE("filament-temps lookup: no published table answers nullopt",
          "[snapmaker][filament-temps]") {
    FilamentTempsFixture fixture;
    CHECK_FALSE(
        filament_temps::lookup_filament_temperatures(slot("Generic", "PETG", "")).has_value());
}

// ============================================================================
// Capability questions over PrinterDiscovery
// ============================================================================

TEST_CASE("filament-temps capability: keyed on the filament_parameters object",
          "[snapmaker][filament-temps]") {
    PrinterDiscovery hw;
    CHECK_FALSE(filament_temps::firmware_publishes_filament_temperatures(hw));
    CHECK(filament_temps::filament_temperature_query_gcode(hw).empty());

    hw.parse_objects(nlohmann::json::array({"extruder", "filament_parameters"}));
    CHECK(filament_temps::firmware_publishes_filament_temperatures(hw));
    CHECK(filament_temps::filament_temperature_query_gcode(hw) == "FILAMENT_PARA_GET_ALL_INFO");

    PrinterDiscovery other;
    other.parse_objects(nlohmann::json::array({"extruder", "heater_bed"}));
    CHECK_FALSE(filament_temps::firmware_publishes_filament_temperatures(other));
    CHECK(filament_temps::filament_temperature_query_gcode(other).empty());
}

// ============================================================================
// Wiring into build_active_material (three-tier precedence, #961)
// ============================================================================

TEST_CASE_METHOD(FilamentTempsFixture,
                 "active material: firmware temps ride along without touching the print range",
                 "[snapmaker][filament-temps][active_material]") {
    store_captured_table();
    auto m = build_active_material(slot("Generic", "PETG", "SnapSpeed"));

    REQUIRE(m.firmware_temps.has_value());
    CHECK(m.firmware_temps->load_c.value() == 270);
    CHECK(m.firmware_temps->unload_c.value() == 270);
    // Load/unload are OPERATION temperatures; the DB's print range for PETG
    // stands untouched beside them.
    CHECK(m.material_info.nozzle_min == 230);
    CHECK(m.material_info.nozzle_max == 260);
}

TEST_CASE_METHOD(FilamentTempsFixture,
                 "active material: a user override still wins with the firmware table present",
                 "[snapmaker][filament-temps][active_material]") {
    filament::MaterialOverride ovr;
    ovr.nozzle_min = 215;
    MaterialSettingsManager::instance().set_override("PETG", ovr);

    store_captured_table();
    auto m = build_active_material(slot("Generic", "PETG", "SnapSpeed"));

    CHECK(m.material_info.nozzle_min == 215); // tier 1: the user's overlay
    CHECK(m.material_info.nozzle_max == 260); // tier 3: DB (no preset, no override)
    REQUIRE(m.firmware_temps.has_value());    // tier 2 data, its own fields
    CHECK(m.firmware_temps->load_c.value() == 270);
}

TEST_CASE_METHOD(FilamentTempsFixture,
                 "active material: no table published leaves firmware_temps unset",
                 "[snapmaker][filament-temps][active_material]") {
    auto m = build_active_material(slot("Generic", "PETG", "SnapSpeed"));
    CHECK_FALSE(m.firmware_temps.has_value());
}

// ============================================================================
// The one consumer: load preheat
// ============================================================================

TEST_CASE_METHOD(FilamentTempsFixture,
                 "load preheat: the firmware's load temp beats the DB midpoint",
                 "[snapmaker][filament-temps][filament][preheat]") {
    const auto db = filament::find_material("PLA");
    REQUIRE(db.has_value());

    // Without a published table the resolver answers the DB midpoint.
    SlotInfo pla_lane = slot("Snapmaker", "PLA", "SnapSpeed");
    auto t = helix::ui::resolve_load_preheat_material(0, &pla_lane, nullptr);
    REQUIRE(t.has_value());
    CHECK(t->temp_c == db->nozzle_recommended());

    store_captured_table();
    t = helix::ui::resolve_load_preheat_material(0, &pla_lane, nullptr);
    REQUIRE(t.has_value());
    CHECK(t->temp_c == 250); // the firmware's own load temperature for this spool
}
