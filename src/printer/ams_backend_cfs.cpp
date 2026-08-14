// SPDX-License-Identifier: GPL-3.0-or-later
#if HELIX_HAS_CFS

#include "ams_backend_cfs.h"

#include "ui_temperature_utils.h"

#include "ams_fault_event.h"
#include "ams_tool_map_sync.h"
#include "filament_catalog.h"
#include "filament_slot_override.h"
#include "filament_slot_override_store.h"
#include "json_utils.h"
#include "lvgl/src/others/translation/lv_translation.h"
#include "moonraker_error.h"
#include "operation_patterns.h" // helix::contains_ci
#include "post_op_cooldown_manager.h"
#include "printer_detector.h"

#include <spdlog/spdlog.h>

#include <cctype>
#include <string>
#include <unordered_map>

#include "hv/json.hpp"

namespace helix::printer {

using json = nlohmann::json;

std::string CfsMaterialDb::strip_code(const std::string& code) {
    if (code == "-1" || code == "None" || code.empty())
        return "";
    if (code.size() == 6 && code[0] == '1')
        return code.substr(1);
    return code;
}

uint32_t CfsMaterialDb::parse_color(const std::string& color_str) {
    if (color_str == "-1" || color_str == "None" || color_str.empty())
        return DEFAULT_COLOR;
    std::string hex = color_str;
    if (hex.size() == 7 && hex[0] == '0')
        hex = hex.substr(1);
    try {
        return static_cast<uint32_t>(std::stoul(hex, nullptr, 16));
    } catch (...) {
        return DEFAULT_COLOR;
    }
}

std::string CfsMaterialDb::slot_to_tnn(int global_index) {
    if (global_index < 0 || global_index > 15)
        return "";
    int unit = global_index / 4 + 1;
    int slot = global_index % 4;
    char letter = 'A' + static_cast<char>(slot);
    return "T" + std::to_string(unit) + letter;
}

int CfsMaterialDb::tnn_to_slot(const std::string& tnn) {
    if (tnn.size() != 3 || tnn[0] != 'T')
        return -1;
    int unit = tnn[1] - '0';
    int slot = tnn[2] - 'A';
    if (unit < 1 || unit > 4 || slot < 0 || slot > 3)
        return -1;
    return (unit - 1) * 4 + slot;
}

// --- CFS Error Decoder ---

namespace {

/// Format the [unit, slot] payload observed in `key849` (and likely all
/// SLOT-level CFS errors) into a human locator. Klipper emits unit as an
/// integer (1-based) and slot as a single uppercase letter ("A"..."D").
/// Returns "" when the payload doesn't match the expected shape — caller
/// then displays the un-augmented message rather than guessing.
///
/// Real-world sample (telemetry 2026-05-05):
///     !! {"code":"key849","values":[1,"B"]}
///   → " in unit 1 slot B"
std::string format_unit_slot(const nlohmann::json& values) {
    if (!values.is_array() || values.size() < 2)
        return "";
    if (!values[0].is_number_integer())
        return "";
    if (!values[1].is_string())
        return "";
    int unit = values[0].get<int>();
    std::string slot = values[1].get<std::string>();
    if (unit < 1 || slot.size() != 1)
        return "";
    char c = slot[0];
    if (c < 'A' || c > 'D')
        return "";
    return " in unit " + std::to_string(unit) + " slot " + slot;
}

/// Format unit-level errors where values is `[unit]` or `[unit, ...]`.
/// We just grab the leading int and ignore tails. Empty string if the
/// shape doesn't match.
std::string format_unit_only(const nlohmann::json& values) {
    if (!values.is_array() || values.empty())
        return "";
    if (!values[0].is_number_integer())
        return "";
    int unit = values[0].get<int>();
    if (unit < 1)
        return "";
    return " on unit " + std::to_string(unit);
}

/// True when a per-slot `vender` string carries no occupancy signal. The box
/// reports one of these for an empty bay; `"none"` (lowercase) is also the
/// synthetic default when the array is short. A REAL vendor name or the
/// present-but-unresolved `"unknown"` marker are NOT sentinels — both mean a
/// spool is seated. Kept in one place so every comparison site stays in sync.
bool is_vender_sentinel(const std::string& v) {
    return v.empty() || v == "none" || v == "None" || v == "-1";
}

/// True when a raw per-slot `material_type` string carries no RFID payload,
/// i.e. the reader has no tag data for this bay. Adds `"unknown"` to the
/// vender sentinel set: for `material_type` "unknown" means the reader has
/// nothing for this bay, whereas for `vender` it means a tag IS seated but its
/// vendor didn't resolve. That asymmetry is real hardware behavior, not an
/// inconsistency — see prestonbrown/helixscreen#1077 and the presence rule in
/// parse_box_status.
///
/// Deliberately NOT applied to `color_value`: that field is user-writable
/// (push_slot_color_to_firmware / the stock LCD both issue BOX_MODIFY_TN_DATA
/// PART=color_value) and reads a real color on untagged bays, so it cannot
/// distinguish a tagged bay from an untagged one. `material_type` has no write
/// path anywhere in the UI, so a non-sentinel value there means — and only
/// means — that a tag was actually read.
bool is_material_code_sentinel(const std::string& v) {
    return v.empty() || v == "none" || v == "None" || v == "-1" || v == "unknown";
}

std::string quote_gcode_param(const std::string& value) {
    std::string quoted;
    quoted.reserve(value.size() + 2);
    quoted += '"';
    for (char c : value) {
        if (c == '\n' || c == '\r') {
            quoted += ' ';
        } else {
            if (c == '\\' || c == '"')
                quoted += '\\';
            quoted += c;
        }
    }
    quoted += '"';
    return quoted;
}

} // namespace

struct CfsErrorEntry {
    const char* message;
    const char* hint;
    AmsAlertLevel level;
    /// Optional formatter that stringifies the `values` array into a
    /// human-readable locator (" in unit 1 slot B"). Caller appends the
    /// result to the friendly message. nullptr = no per-error format
    /// known yet (no regression — message displays unchanged).
    std::string (*format_values)(const nlohmann::json&) = nullptr;
};

// Format-callback aliases keep the table tidy. Only key849 has been
// confirmed against real telemetry (`[1,"B"]`); the other SLOT/UNIT
// codes are wired up to the same formatters on the assumption that
// Creality uses a consistent shape, but they'll degrade gracefully
// (no extra locator displayed) if the assumption is wrong.
static auto* const fmt_unit_slot = &format_unit_slot;
static auto* const fmt_unit_only = &format_unit_only;

static const std::unordered_map<std::string, CfsErrorEntry> CFS_ERROR_TABLE = {
    // Klipper-internal errors (not CFS-specific) that we frequently surface to
    // users. Despite living in the CFS table for now, these are general — the
    // table predates the broader use case. TODO: rename to KlipperErrorTable.
    {"key111",
     {"Pre-heat the extruder first",
      "Filament can't be loaded below the minimum extrude temperature. Set a hotend target (e.g. "
      "220°C for PLA, 240°C for PETG), wait for it to reach temperature, then try again",
      AmsAlertLevel::SYSTEM, nullptr}},
    {"key298",
     {"MCU bridge daemon is shut down",
      "Tap Firmware Restart to recover — on K2 this also bounces the rpi MCU bridge",
      AmsAlertLevel::SYSTEM, nullptr}},
    {"key585",
     {"Move out of range", "The requested position is outside the printer's bounds",
      AmsAlertLevel::SYSTEM, nullptr}},

    // Motor controller init errors (motor_control_wrapper.so). Typically fire
    // during CFS bring-up. Users can't fix these directly — power cycle is the
    // standard remedy. Surfacing them avoids the raw chinglish from Creality
    // ("Motor set pin restore io status error").
    {"key800",
     {"Motor controller error",
      "A motor IO pin couldn't be restored. Power-cycle the printer to recover",
      AmsAlertLevel::SYSTEM, nullptr}},
    {"key801",
     {"Z motor calibration failed",
      "Stall detection setup failed for the Z motor — power-cycle to retry", AmsAlertLevel::SYSTEM,
      nullptr}},
    {"key802",
     {"Extruder motor calibration failed",
      "Stall detection setup failed for the extruder — power-cycle to retry", AmsAlertLevel::SYSTEM,
      nullptr}},
    {"key803",
     {"Motor parameter setup failed",
      "A motor's parameters couldn't be written. Power-cycle the printer to retry",
      AmsAlertLevel::SYSTEM, nullptr}},

    {"key831",
     {"Lost connection to CFS unit", "Check the RS-485 cable between printer and CFS",
      AmsAlertLevel::SYSTEM, nullptr}},
    {"key834",
     {"Invalid parameters sent to CFS", "This may indicate a firmware bug — try restarting",
      AmsAlertLevel::SYSTEM, nullptr}},
    {"key835",
     {"Filament jammed at CFS connector",
      "Open the CFS lid, check the PTFE tube connection for the stuck slot", AmsAlertLevel::SLOT,
      fmt_unit_slot}},
    {"key836",
     {"Filament jammed between CFS and sensor", "Check the Bowden tube for kinks or debris",
      AmsAlertLevel::SLOT, fmt_unit_slot}},
    {"key837",
     {"Filament jammed before extruder gear",
      "Check for tangles on the spool and clear the filament path to the printhead",
      AmsAlertLevel::SLOT, fmt_unit_slot}},
    {"key838",
     {"Filament reached extruder but won't feed",
      "Check for a clog in the hotend or a worn drive gear", AmsAlertLevel::SLOT, fmt_unit_slot}},
    {"key839",
     {"No filament detected at CFS extrude position",
      "The selected slot may be empty or the filament didn't reach the CFS extruder",
      AmsAlertLevel::SLOT, fmt_unit_slot}},
    {"key840",
     {"CFS unit state error", "A unit reported an unexpected state — check its current operation",
      AmsAlertLevel::UNIT, fmt_unit_only}},
    {"key841",
     {"Filament cutter stuck",
      "The cutter blade didn't return — check for filament wrapped around the cutting mechanism",
      AmsAlertLevel::SYSTEM, nullptr}},
    {"key843",
     {"Can't read filament RFID tag",
      "Re-seat the spool in the slot, ensure the RFID label faces the reader", AmsAlertLevel::SLOT,
      fmt_unit_slot}},
    {"key844",
     {"PTFE tube connection loose", "Re-seat the Bowden tube connector on the CFS unit",
      AmsAlertLevel::UNIT, fmt_unit_only}},
    {"key845",
     {"Nozzle clog detected", "Run a cold pull or replace the nozzle", AmsAlertLevel::SYSTEM,
      nullptr}},
    {"key846",
     {"Empty print detected — feed rate too slow",
      "CFS feed rate fell below extruder demand. The spool may be empty or jammed",
      AmsAlertLevel::SYSTEM, nullptr}},
    {"key847",
     {"Empty spool — filament wound around hub",
      "Remove the empty spool and clear wound filament from the CFS hub", AmsAlertLevel::SLOT,
      fmt_unit_slot}},
    {"key848",
     {"Filament snapped inside CFS",
      "Open the CFS unit and remove the broken filament from the slot", AmsAlertLevel::SLOT,
      fmt_unit_slot}},
    {"key849",
     {"Retract failed — filament stuck in connector",
      "Manually pull the filament back through the connector", AmsAlertLevel::SLOT, fmt_unit_slot}},
    {"key850",
     {"Retract error — multiple connectors triggered",
      "Check that only one filament path is active", AmsAlertLevel::UNIT, fmt_unit_only}},
    {"key851",
     {"Retract didn't reach buffer empty position",
      "The filament may not have fully retracted — try again or manually pull", AmsAlertLevel::SLOT,
      fmt_unit_slot}},
    {"key852",
     {"Sensor mismatch — check extruder and CFS sensors",
      "Extruder and CFS disagree on filament state — inspect both sensors", AmsAlertLevel::SYSTEM,
      nullptr}},
    {"key853",
     {"Humidity sensor malfunction",
      "CFS unit's humidity sensor is not responding — may need service", AmsAlertLevel::UNIT,
      fmt_unit_only}},
    {"key854",
     {"Cutter blade didn't sever filament",
      "Filament is still present after the cut — the blade may be dull or misaligned",
      AmsAlertLevel::SYSTEM, nullptr}},
    {"key855",
     {"Filament cutter position error",
      "The cutter is out of alignment — recalibrate with CALIBRATE_CUT_POS", AmsAlertLevel::SYSTEM,
      nullptr}},
    {"key856",
     {"Filament cutter not detected", "Check that the cutter mechanism is properly installed",
      AmsAlertLevel::SYSTEM, nullptr}},
    {"key857",
     {"CFS motor overloaded", "A spool may be tangled or the drive gear is jammed",
      AmsAlertLevel::UNIT, fmt_unit_only}},
    {"key858",
     {"EEPROM error on CFS unit", "CFS unit storage is corrupted — may need firmware reflash",
      AmsAlertLevel::UNIT, fmt_unit_only}},
    {"key859",
     {"Measuring wheel error", "The filament length sensor is malfunctioning", AmsAlertLevel::UNIT,
      fmt_unit_only}},
    {"key860",
     {"Buffer tube problem", "Check the buffer unit on the back of the printer",
      AmsAlertLevel::SYSTEM, nullptr}},
    {"key861",
     {"RFID reader malfunction (left)", "The left RFID reader in this CFS unit may need service",
      AmsAlertLevel::UNIT, fmt_unit_only}},
    {"key862",
     {"RFID reader malfunction (right)", "The right RFID reader in this CFS unit may need service",
      AmsAlertLevel::UNIT, fmt_unit_only}},
    {"key863",
     {"Retract error — filament still detected",
      "Filament didn't fully retract, may need manual removal", AmsAlertLevel::SLOT,
      fmt_unit_slot}},
    {"key864",
     {"Extrude error — buffer not full", "Filament didn't fill buffer tube during load",
      AmsAlertLevel::SLOT, fmt_unit_slot}},
    {"key865",
     {"Retract error — failed to exit connector", "Filament stuck in connector during unload",
      AmsAlertLevel::SLOT, fmt_unit_slot}},
    // key866 is a second "no cutter" variant emitted from the motor driver path
    // (key856 comes from the box driver). Same root cause, same remedy.
    {"key866",
     {"Filament cutter not detected", "Check that the cutter mechanism is properly installed",
      AmsAlertLevel::SYSTEM, nullptr}},
};

std::optional<std::pair<const char*, const char*>>
CfsErrorDecoder::lookup_message(const std::string& key_code) {
    auto it = CFS_ERROR_TABLE.find(key_code);
    if (it == CFS_ERROR_TABLE.end())
        return std::nullopt;
    return std::make_pair(it->second.message, it->second.hint);
}

std::optional<std::pair<std::string, std::string>>
CfsErrorDecoder::lookup_message_with_values(const std::string& key_code,
                                            const nlohmann::json& values) {
    auto it = CFS_ERROR_TABLE.find(key_code);
    if (it == CFS_ERROR_TABLE.end())
        return std::nullopt;
    const auto& entry = it->second;
    std::string message = entry.message;
    if (entry.format_values) {
        std::string locator = entry.format_values(values);
        if (!locator.empty()) {
            message += locator;
        }
    }
    return std::make_pair(std::move(message), std::string(entry.hint));
}

std::optional<AmsAlert> CfsErrorDecoder::decode(const std::string& key_code, int unit_index,
                                                int slot_index) {
    auto it = CFS_ERROR_TABLE.find(key_code);
    if (it == CFS_ERROR_TABLE.end())
        return std::nullopt;

    const auto& entry = it->second;
    AmsAlert alert;
    alert.message = entry.message;
    alert.hint = entry.hint;
    alert.level = entry.level;
    alert.severity = SlotError::Severity::ERROR;

    // Extract numeric code: "key845" -> "CFS-845"
    if (key_code.size() > 3) {
        alert.error_code = "CFS-" + key_code.substr(3);
    }

    if (entry.level == AmsAlertLevel::UNIT || entry.level == AmsAlertLevel::SLOT) {
        alert.unit_index = unit_index;
    }
    if (entry.level == AmsAlertLevel::SLOT) {
        alert.slot_index = slot_index;
    }

    return alert;
}

// =============================================================================
// AmsBackendCfs — Main CFS backend class
// =============================================================================

AmsBackendCfs::AmsBackendCfs(IMoonrakerAPI* api, helix::IMoonrakerClient* client)
    : AmsSubscriptionBackend(api, client) {
    system_info_.type = AmsType::CFS;
    system_info_.type_name = "CFS";
    system_info_.supports_bypass = false;
    system_info_.tip_method = TipMethod::CUT;
    system_info_.supports_purge = true;

    // Latch K1 vs K2 macro dialect once. PrinterDetector reads the resolved
    // printer type straight from Config, which is the only source guaranteed
    // populated this early: the CFS backend is constructed in the discovery
    // callback (init_subsystems_from_hardware) BEFORE auto_detect_and_save runs.
    // Reading from Config here is therefore deliberate — do not move this to a
    // capability snapshot that is populated later in the discovery sequence, or
    // a saved-type K1/Hi relaunch would mis-route to the K2 dialect. #968.
    //
    // The Creality Hi ships the non-prefixed BOX_* dialect in its box.cfg
    // (BOX_CUT_MATERIAL / BOX_EXTRUDE_MATERIAL / BOX_RETRUDE_MATERIAL /
    // BOX_GO_TO_EXTRUDE_POS / BOX_SAVE_FAN / BOX_RESTORE_FAN), i.e. the K1
    // family — NOT the K2 CR_BOX_* primitives. Route it to the K1 variant so
    // load/unload/swap dispatch resolves to macros the firmware actually
    // defines.
    macro_variant_ = (PrinterDetector::is_creality_k1() || PrinterDetector::is_creality_hi())
                         ? CfsMacroVariant::K1
                         : CfsMacroVariant::K2;

    spdlog::info("[AMS CFS] Backend created — macro variant: {}",
                 macro_variant_ == CfsMacroVariant::K1 ? "K1 (BOX_*)" : "K2 (CR_BOX_*)");
}

// --- Sensor Ownership (#1054) ---

bool AmsBackendCfs::owns_filament_sensor(const std::string& bare_name,
                                         const helix::PrinterDiscovery& discovery) {
    (void)discovery; // CFS owns a single fixed name; no discovery needed.
    // K2 CFS exposes one filament_switch_sensor at the toolhead with the bare
    // name "filament_sensor". Conventional elsewhere, so only claimed when CFS
    // is the detected backend.
    return bare_name == "filament_sensor";
}

void AmsBackendCfs::on_started() {
    spdlog::info("[AMS CFS] Backend started — querying initial box state");

    // Load persisted per-slot overrides from the shared FilamentSlotOverrideStore
    // BEFORE issuing the initial status query — otherwise the first status
    // callback (libhv background thread) could fire and parse slots before
    // overrides_ is populated, so the first EVENT_STATE_CHANGED frame would
    // miss override data. load_blocking runs on this (main) thread; the
    // Moonraker DB callback fires on the libhv event loop, so the two threads
    // don't interfere. Migration from helix-screen:cfs_slot_overrides to
    // lane_data happens automatically inside load_blocking the first time
    // lane_data is empty (Task 8).
    if (api_) {
        override_store_ = std::make_unique<helix::ams::FilamentSlotOverrideStore>(
            api_, "cfs", helix::ams::lane_key_style_for(get_type()));
        // Do the (potentially 5s) MR DB round-trip OUTSIDE the lock, then swap
        // in under mutex_. Holding mutex_ during the swap ensures the parse
        // path sees a coherent map rather than a torn write.
        auto loaded = override_store_->load_blocking();
        const auto loaded_count = loaded.size();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            overrides_ = std::move(loaded);
        }
        spdlog::info("[AMS CFS] Loaded {} slot overrides from filament_slot store", loaded_count);
    }

    // Query initial state explicitly since the subscription response may have
    // arrived before this backend registered its callback
    nlohmann::json objects_to_query = nlohmann::json::object();
    objects_to_query["box"] = nullptr;
    objects_to_query["motor_control"] = nullptr;
    objects_to_query["filament_switch_sensor filament_sensor"] = nullptr;
    // Phase synthesis (Task #2) needs target rises to detect purge phase
    objects_to_query["extruder"] = nlohmann::json::array({"target", "temperature"});

    nlohmann::json params = {{"objects", objects_to_query}};

    client_->send_jsonrpc(
        "printer.objects.query", params,
        [this](const nlohmann::json& response) {
            if (response.contains("result") && response["result"].contains("status") &&
                response["result"]["status"].is_object()) {
                // Wrap in notify_status_update format for handle_status_update
                nlohmann::json notification = {
                    {"params", nlohmann::json::array({response["result"]["status"]})}};
                handle_status_update(notification);
                spdlog::info("[AMS CFS] Initial state loaded");
            } else {
                spdlog::warn("[AMS CFS] Initial state query returned unexpected format");
            }
        },
        [](const MoonrakerError& err) {
            spdlog::warn("[AMS CFS] Failed to query initial state: {}", err.message);
        });
}

// --- Static parser ---

CfsSchema AmsBackendCfs::detect_schema(const nlohmann::json& box_json) {
    if (!box_json.is_object()) {
        return CfsSchema::Stock;
    }
    // A `T{n}` key is decisive for Stock: no flat payload carries one, and if a
    // future firmware ever carried both, the stock parser has the richer decode
    // (material codes, RFID UIDs, tool map) so it should win.
    for (int n = 1; n <= 4; ++n) {
        if (box_json.contains("T" + std::to_string(n))) {
            return CfsSchema::Stock;
        }
    }
    // is_array(), not contains(): a disconnected-unit sentinel could put a
    // scalar "-1" here, and a string json has size() == 1, which would let a
    // bounds-checked loop index into it and throw type_error.305.
    auto it = box_json.find("slots");
    if (it != box_json.end() && it->is_array()) {
        return CfsSchema::Flat;
    }
    return CfsSchema::Stock;
}

AmsSystemInfo AmsBackendCfs::parse_box_status(const nlohmann::json& box_json) {
    return detect_schema(box_json) == CfsSchema::Flat ? parse_flat_box_status(box_json)
                                                      : parse_stock_box_status(box_json);
}

AmsSystemInfo AmsBackendCfs::parse_stock_box_status(const nlohmann::json& box_json) {
    AmsSystemInfo info;
    info.type = AmsType::CFS;
    info.type_name = "CFS";
    info.tip_method = TipMethod::CUT;
    info.supports_purge = true;
    info.supports_bypass = false;

    // Parse auto_refill → endless spool support.
    //
    // Top-level box fields (auto_refill, filament, enable, filament_useup) are
    // documented as JSON ints, unlike the per-unit scalars further down, which are
    // documented as strings (CREALITY_K2_SUPPORT.md § "Moonraker Object: box").
    // safe_int is therefore belt-and-braces here rather than a known-shape fix:
    // .value("auto_refill", 0) still throws type_error.302 on a null or a string,
    // and that throw escapes parse_box_status and drops the entire box frame.
    //
    // Test for an explicit 1 rather than != 0 because safe_int parses numeric
    // strings: were the box to ever report the "-1" sentinel here (the documented
    // absence marker everywhere else in this object), != 0 would read it as TRUE.
    info.endless_spool_enabled = helix::json_util::safe_int(box_json, "auto_refill", 0) == 1;
    info.supports_tool_mapping = true;

    // box.filament is a stale active-lane SELECTION index, NOT a "filament
    // loaded" truth — it retains a lane number even when nothing is loaded,
    // producing phantom "loaded from lane N". The authoritative loaded signal
    // is the toolhead sensor (filament_switch_sensor filament_sensor), handled
    // in handle_status_update. Leave filament_loaded false here.
    info.filament_loaded = false;

    // Runout signal: box.filament_useup == 1 means no filament at the box gate.
    // Raw firmware value here; ams_state gates display on print-paused state to
    // avoid false positives at pre-load print start (see filament_useup decode).
    // safe_int + "== 1" for the same reasons as auto_refill above: a null or
    // wrong-typed value must not throw, and a "-1" must not read as a runout.
    // "== 1" also matches this field's decode exactly, per the line above.
    info.filament_runout = helix::json_util::safe_int(box_json, "filament_useup", 0) == 1;

    // Parse tool mapping from "map" object
    if (box_json.contains("map") && box_json["map"].is_object()) {
        // Find maximum tool index to size the mapping vector
        int max_tool = -1;
        for (auto& [tnn_key, tnn_val] : box_json["map"].items()) {
            int slot = CfsMaterialDb::tnn_to_slot(tnn_key);
            if (slot >= 0 && slot > max_tool) {
                max_tool = slot;
            }
        }
        if (max_tool >= 0) {
            info.tool_to_slot_map.resize(max_tool + 1, -1);
            for (auto& [tnn_key, tnn_val] : box_json["map"].items()) {
                // The map value is a "TnnA" string; anything else (null for an
                // unmapped tool, or a number) would make get<std::string>()
                // throw type_error.302 out of parse_box_status and drop the
                // whole box frame. Skip the entry instead — same is_string()
                // discipline as the same_material group parse below.
                if (!tnn_val.is_string()) {
                    continue;
                }
                int src = CfsMaterialDb::tnn_to_slot(tnn_key);
                int dst = CfsMaterialDb::tnn_to_slot(tnn_val.get<std::string>());
                if (src >= 0 && dst >= 0 && src < static_cast<int>(info.tool_to_slot_map.size())) {
                    info.tool_to_slot_map[src] = dst;
                }
            }
        }
    }

    // Build same_material lookup: material_type code -> material name string
    // "same_material" contains groups like ["101001", "0000000", ["T1A"], "PLA"]
    // where the last element is a human-readable material name
    std::unordered_map<std::string, std::string> same_material_names;
    if (box_json.contains("same_material") && box_json["same_material"].is_array()) {
        for (const auto& group : box_json["same_material"]) {
            if (group.is_array() && group.size() >= 4 && group[0].is_string() &&
                group[3].is_string()) {
                same_material_names[group[0].get<std::string>()] = group[3].get<std::string>();
            }
        }
    }

    // Transient: materialize ONLY the cfs-coded slice for this parse pass.
    const auto cfs_catalog = FilamentCatalog::load_codes("cfs");

    // Loop over T1-T4 units
    for (int n = 1; n <= 4; ++n) {
        std::string key = "T" + std::to_string(n);
        if (!box_json.contains(key) || !box_json[key].is_object()) {
            spdlog::debug("[AMS CFS] {} not present or not an object", key);
            continue;
        }

        const auto& unit_json = box_json[key];
        // safe_string, not .value(): the per-unit scalars below are documented as
        // strings (CREALITY_K2_SUPPORT.md § "Per-Unit Fields"), but .value() with a
        // string default THROWS type_error.302 on a JSON null or a numeric value,
        // and that throw escapes parse_box_status and drops the whole box frame.
        // safe_string returns the supplied default for null/wrong-type WITHOUT
        // stringifying, so a numeric -1 yields the literal default rather than the
        // string "-1" — the sentinel comparisons below stay exact either way.
        std::string state = helix::json_util::safe_string(unit_json, "state", "None");
        if (state == "None" || state == "-1") {
            spdlog::debug("[AMS CFS] {} disconnected (state={})", key, state);
            continue; // Disconnected unit
        }

        spdlog::debug("[AMS CFS] {} connected (state={})", key, state);

        AmsUnit unit;
        unit.unit_index = n - 1;
        unit.name = key;
        unit.display_name = "CFS Unit " + std::to_string(n);
        unit.slot_count = 4;
        unit.first_slot_global_index = (n - 1) * 4;
        unit.connected = true;
        unit.topology = PathTopology::HUB;

        // Firmware version and serial
        std::string ver = helix::json_util::safe_string(unit_json, "version", "-1");
        if (ver != "-1" && ver != "None") {
            unit.firmware_version = ver;
        }
        std::string sn = helix::json_util::safe_string(unit_json, "sn", "-1");
        if (sn != "-1" && sn != "None") {
            unit.serial_number = sn;
        }

        // Environment: temperature and humidity. Both are documented as STRINGS
        // ("27", "48"), so safe_string is the faithful read. Note the residual: if
        // a firmware variant ever sent these as numbers, safe_string yields "None"
        // and the unit silently reports no environment data — better than today's
        // throw, which discards every other field on the box too, but it is a
        // silent loss. Switch to safe_float here if a numeric variant is ever seen.
        std::string temp_str = helix::json_util::safe_string(unit_json, "temperature", "None");
        std::string humid_str =
            helix::json_util::safe_string(unit_json, "dry_and_humidity", "None");
        if (temp_str != "None" && temp_str != "-1" && humid_str != "None" && humid_str != "-1") {
            EnvironmentData env;
            try {
                env.temperature_c = std::stof(temp_str);
            } catch (...) {
                env.temperature_c = 0.0f;
            }
            try {
                env.humidity_pct = std::stof(humid_str);
            } catch (...) {
                env.humidity_pct = 0.0f;
            }
            env.has_humidity = true;
            unit.environment = env;
        }

        // Parse the 4 slots within this unit.
        //
        // These four keys are array[4] on a connected unit, but a unit that drops
        // out reports the SCALAR sentinel "-1"/"None" for the very same keys
        // (CREALITY_K2_SUPPORT.md § "Disconnected Units"; the sibling
        // `filament_rack` object is documented with exactly that scalar form).
        // .value(key, json::array()) does not throw on a scalar — it round-trips
        // through get<json> and hands back the string — but a string json has
        // size() == 1, so the `i < size()` bounds check in the loop below passes
        // for i == 0 and then operator[](size_type) throws type_error.305, BEFORE
        // the .is_string() guard on that same line can run. Normalizing a
        // non-array to an empty array makes every slot fall back to its sentinel
        // default instead. Same is_array() discipline as build_cfs_slot_uid's
        // `pick` lambda below.
        auto array_field = [&unit_json](const char* field) -> nlohmann::json {
            auto it = unit_json.find(field);
            if (it == unit_json.end() || !it->is_array())
                return nlohmann::json::array();
            return *it;
        };
        auto color_arr = array_field("color_value");
        auto material_arr = array_field("material_type");
        auto remain_arr = array_field("remain_len");
        auto vender_arr = array_field("vender");

        for (int i = 0; i < 4; ++i) {
            SlotInfo slot;
            slot.slot_index = i;
            slot.global_index = (n - 1) * 4 + i;

            // Color
            std::string color_str = "-1";
            if (i < static_cast<int>(color_arr.size()) && color_arr[i].is_string()) {
                color_str = color_arr[i].get<std::string>();
            }
            slot.color_rgb = CfsMaterialDb::parse_color(color_str);

            // Material type
            std::string mat_code_raw = "-1";
            if (i < static_cast<int>(material_arr.size()) && material_arr[i].is_string()) {
                mat_code_raw = material_arr[i].get<std::string>();
            }
            std::string mat_id = CfsMaterialDb::strip_code(mat_code_raw);
            if (!mat_id.empty()) {
                const auto* mat_info = cfs_catalog.resolve_code("cfs", mat_id);
                if (mat_info) {
                    slot.material = mat_info->type;
                    slot.brand = mat_info->brand;
                    slot.nozzle_temp_min = mat_info->nozzle_min;
                    slot.nozzle_temp_max = mat_info->nozzle_max;
                } else {
                    // Fallback: check same_material for a human-readable name
                    auto it = same_material_names.find(mat_code_raw);
                    if (it != same_material_names.end()) {
                        slot.material = it->second;
                    }
                }
            }

            // Per-slot vendor string. Unlike color_value/material_type — which
            // are LATCHED RFID data that stay pinned to the last spool after
            // removal and thus fake "ghost" slots — `vender` reads a real vendor
            // name or the present-but-unresolved "unknown" when a spool is
            // seated, and a sentinel ("none"/"None"/"-1") when the bay is empty
            // (verified against K2 Plus hardware; prestonbrown/helixscreen#1077).
            std::string vender_str = "none";
            if (i < static_cast<int>(vender_arr.size()) && vender_arr[i].is_string()) {
                vender_str = vender_arr[i].get<std::string>();
            }
            const bool vender_occupied = !is_vender_sentinel(vender_str);

            // Brand fallback for RFID spools whose material code isn't in our
            // DB (the DB lookup above already sets slot.brand for known codes).
            // Priority for a vendor-occupied bay:
            //   1. a real vendor name reported by the box, else
            //   2. "Creality" when the tag is present but the vendor is
            //      "unknown" — CFS RFID tags are Creality's proprietary
            //      ecosystem, so a present-but-unresolved tag is a Creality
            //      spool. A user override still wins over this.
            if (slot.brand.empty() && vender_occupied) {
                slot.brand = (vender_str == "unknown") ? "Creality" : vender_str;
            }

            // Remaining length
            std::string remain_str = "-1";
            if (i < static_cast<int>(remain_arr.size()) && remain_arr[i].is_string()) {
                remain_str = remain_arr[i].get<std::string>();
            }
            if (remain_str != "-1" && remain_str != "None") {
                try {
                    slot.remaining_length_m = std::stof(remain_str);
                } catch (...) {
                    slot.remaining_length_m = 0.0f;
                }
            }

            // Presence, in priority order. Each firmware field is used only
            // where it is actually trustworthy:
            //
            //  1. `vender` is the LIVE occupancy signal for a TAGGED bay — it
            //     reads a real vendor name (or the present-but-unresolved
            //     "unknown") while a tag is seated and drops to a sentinel the
            //     moment the spool is pulled. Non-sentinel => PRESENT.
            //
            //  2. `remain_len` is a FALLBACK that exists solely to cover
            //     UNTAGGED 3rd-party spools: they have no RFID vendor, so
            //     `vender` reads sentinel even though a spool is physically
            //     seated, and the measuring wheel is the only evidence we get
            //     (prestonbrown/helixscreen#1077, commit 65b3a1b8d).
            //
            //     But `remain_len` LATCHES: after a tagged spool is removed the
            //     box keeps reporting its last length forever (observed on K2:
            //     vender "none" alongside remain_len "50"). Applying the
            //     untagged fallback there pins the bay AVAILABLE permanently.
            //
            //     A bay carrying a real latched RFID material code
            //     (`material_type` past its sentinels) is by definition a
            //     TAGGED bay, and the untagged fallback does not apply to it —
            //     for those, `vender` alone decides. So the fallback is
            //     consulted only when no tag was ever read, which is exactly
            //     the untagged case it was written for.
            //
            //     `color_value` is deliberately NOT part of this test even
            //     though it also latches. It is user-writable (the stock LCD
            //     and our own push_slot_color_to_firmware both write it) and on
            //     real K2 hardware it reads a real color on EVERY bay, tagged
            //     or not — including bays whose material_type is "unknown".
            //     Folding it in would make has_tag_payload permanently true,
            //     silently disabling the untagged fallback and re-breaking
            //     #1077. material_type has no write path, so it alone is a
            //     trustworthy tagged/untagged discriminator.
            //
            // A user override can still promote a firmware-EMPTY bay back to
            // AVAILABLE (see apply_overrides).
            const bool remain_present = slot.remaining_length_m > 0.0f;
            const bool has_tag_payload = !is_material_code_sentinel(mat_code_raw);
            const bool untagged_present = !has_tag_payload && remain_present;
            if (vender_occupied || untagged_present) {
                slot.status = SlotStatus::AVAILABLE;
            } else {
                slot.status = SlotStatus::EMPTY;
                // Scrub the latched display fields parse populated so a removed
                // spool's stale color/material doesn't render on the empty bay.
                // This resets only PARSED firmware fields — not the persistent
                // user override, which apply_overrides/clear_override_locked own.
                slot.brand.clear();
                slot.material.clear();
                slot.color_name.clear();
                slot.color_rgb = CfsMaterialDb::parse_color("-1");
                slot.remaining_length_m = 0.0f;
            }

            // mapped_tool is deliberately NOT written here. box.map, parsed
            // above into info.tool_to_slot_map, is the box's own statement of
            // the mapping, and the single sync_tool_map_from_forward() pass at
            // the end of this function derives every slot's mapped_tool from
            // it. Stamping the identity here as well is what made a
            // BOX_MODIFY_TN remap show the right op-button lane and the wrong
            // lane badge — two writers, one of them ignoring firmware.

            unit.slots.push_back(std::move(slot));
        }

        // Parse active filament slot within this unit.
        // T1.filament = "A"/"B"/"C"/"D" when a slot is loaded, "None" otherwise.
        std::string fil_letter = helix::json_util::safe_string(unit_json, "filament", "None");
        if (fil_letter.size() == 1 && fil_letter[0] >= 'A' && fil_letter[0] <= 'D') {
            int active_local = fil_letter[0] - 'A';
            info.current_slot = (n - 1) * 4 + active_local;
            spdlog::debug("[AMS CFS] Active filament: {} (slot {})", fil_letter, info.current_slot);
        }

        info.units.push_back(std::move(unit));
        info.total_slots += 4;
    }

    // Publish both directions from the one source the box actually states —
    // box.map. identity_fallback=true keeps the historical 1:1 default for
    // every tool the payload leaves unmapped (and for the common case of no
    // `map` key at all), so a box that has never been remapped parses exactly
    // as it always did.
    sync_tool_map_from_forward(info, /*identity_fallback=*/true);

    // current_tool must name the tool that ROUTES THROUGH the seated lane, not
    // the lane index: with T0 remapped onto lane 2 the print-status color dot
    // has to read T0. Derived from the same pass above, so it cannot disagree
    // with the badge.
    //
    // A seated lane that no tool maps to (possible from a PARTIAL box.map,
    // where identity_fallback deliberately refuses to hand a claimed lane back)
    // falls back to the lane index — the pre-remap answer — rather than
    // publishing -1. -1 is a legitimate value of this field, but only in
    // combination with "nothing loaded", and no consumer renders the
    // seated-yet-toolless pair usefully: ams_current_tool.xml hides the whole
    // indicator on `< 0`, taking the still-valid filament colour swatch with
    // it, and ToolState::set_ams_topology() clamps a negative active_tool to 0,
    // which lights T0 in the tool switcher and the nozzle label while a
    // different lane is physically seated. A stale-but-plausible tool number
    // beats both of those.
    if (info.current_slot >= 0) {
        const auto* seated = info.get_slot_global(info.current_slot);
        const int seated_tool = seated ? seated->mapped_tool : -1;
        info.current_tool = seated_tool >= 0 ? seated_tool : info.current_slot;
    }

    return info;
}

// --- Flat schema (community Kalico box.py reimplementations) ---------------

// Parse a conventional "#RRGGBB" (or bare "RRGGBB") into 0xRRGGBB.
//
// NOT CfsMaterialDb::parse_color: that one owns Creality's leading-zero
// "0RRGGBB" form and its "-1"/"None" sentinels. The flat schema uses the
// ordinary web spelling, and marks "no color" with an empty string (the
// external-spool entry). Anything unparseable — wrong length, non-hex digits,
// empty — yields the default slot color rather than a partial read, so a
// malformed field renders as "unknown color" instead of a plausible wrong one.
static uint32_t parse_flat_slot_color(const std::string& raw) {
    std::string s = raw;
    if (!s.empty() && s[0] == '#') {
        s.erase(0, 1);
    }
    if (s.size() != 6) {
        return AMS_DEFAULT_SLOT_COLOR;
    }
    for (char c : s) {
        if (std::isxdigit(static_cast<unsigned char>(c)) == 0) {
            return AMS_DEFAULT_SLOT_COLOR;
        }
    }
    return static_cast<uint32_t>(std::stoul(s, nullptr, 16));
}

AmsSystemInfo AmsBackendCfs::parse_flat_box_status(const nlohmann::json& box_json) {
    AmsSystemInfo info;
    info.type = AmsType::CFS;
    info.type_name = "CFS";
    info.tip_method = TipMethod::CUT;
    info.supports_purge = true;

    // Bypass stays unsupported even though slots[] carries an `external: true`
    // entry for the spool holder. The entry is observable, but the port's
    // box.py is unpublished, so no verified command drives a load from it —
    // advertising bypass would put a button on screen that cannot work.
    info.supports_bypass = false;

    // runout_swap_enabled is the fork's endless-spool flag. safe_bool rather
    // than .value(): every scalar in this object is nullable (`runout` itself
    // ships as JSON null when idle).
    info.endless_spool_enabled =
        helix::json_util::safe_bool(box_json, "runout_swap_enabled", false);
    info.supports_tool_mapping = true;

    // `runout` is null while idle and carries a slot descriptor once tripped;
    // presence of any non-null value is the runout signal. filament_loaded is
    // left false here for the same reason as the stock parse — the toolhead
    // sensor branch in handle_status_update is its sole writer.
    auto runout_it = box_json.find("runout");
    info.filament_runout = runout_it != box_json.end() && !runout_it->is_null();
    info.filament_loaded = false;

    // Per-material recommended temps. The fork publishes a materials table the
    // stock firmware has no equivalent for; it is keyed by the same material
    // string each slot reports, so it resolves without a code table.
    // A single target_temp is all the firmware gives, so min == max: a
    // degenerate range is truthful, whereas leaving min at 0 would render as
    // "0-300°C".
    std::unordered_map<std::string, int> material_temps;
    auto materials_it = box_json.find("materials");
    if (materials_it != box_json.end() && materials_it->is_object()) {
        for (const auto& [name, spec] : materials_it->items()) {
            if (!spec.is_object()) {
                continue;
            }
            int t = helix::json_util::safe_int(spec, "target_temp", 0);
            if (t > 0) {
                material_temps[name] = t;
            }
        }
    }

    AmsUnit unit;
    unit.unit_index = 0;
    unit.name = "CFS";
    unit.display_name = "CFS Unit 1";
    unit.first_slot_global_index = 0;
    unit.connected = helix::json_util::safe_bool(box_json, "driver_ready", true);
    unit.topology = PathTopology::HUB;

    // Environment. Unlike the stock schema these are JSON numbers, not strings,
    // but safe_float accepts either — so a firmware revision that switches
    // spelling will not silently drop the readings.
    float temp_c = helix::json_util::safe_float(box_json, "temp_c", -1.0f);
    float humidity = helix::json_util::safe_float(box_json, "humidity_pct", -1.0f);
    if (temp_c >= 0.0f || humidity >= 0.0f) {
        EnvironmentData env;
        env.temperature_c = temp_c >= 0.0f ? temp_c : 0.0f;
        env.humidity_pct = humidity >= 0.0f ? humidity : 0.0f;
        env.has_humidity = humidity >= 0.0f;
        unit.environment = env;
    }

    // load_path describes the shared feed path: an encoder, a buffer, and a
    // printhead sensor. Stock CFS reports none of these, so these flags light
    // up path visualization that the stock backend cannot drive.
    auto path_it = box_json.find("load_path");
    if (path_it != box_json.end() && path_it->is_object()) {
        const auto& path = *path_it;

        auto encoder_it = path.find("encoder");
        unit.has_encoder = encoder_it != path.end() && encoder_it->is_object();

        auto sensor_it = path.find("printhead_sensor");
        unit.has_toolhead_sensor = sensor_it != path.end() && sensor_it->is_object();

        auto buffer_it = path.find("buffer");
        if (buffer_it != path.end() && buffer_it->is_object()) {
            BufferHealth buffer;
            // `active` is the fork's enable flag. state_code/status_code are
            // small enums whose meanings are NOT documented anywhere we can
            // read (box.py is unpublished), so they are deliberately NOT
            // decoded into a state string — an invented mapping would surface
            // confident nonsense in the UI. Only the enable flag is trusted.
            buffer.fault_detection_enabled =
                helix::json_util::safe_bool(*buffer_it, "active", false);
            unit.buffer_health = buffer;
        }
    }

    // Slots. Each entry is self-describing — no parallel arrays, no material
    // code table, no TNN letter math.
    auto slots_it = box_json.find("slots");
    if (slots_it != box_json.end() && slots_it->is_array()) {
        for (const auto& slot_json : *slots_it) {
            // A non-object entry (a "-1" sentinel, say) is skipped rather than
            // aborting the frame — same discipline as the stock array_field
            // guard. Skipping keeps later slots parseable.
            if (!slot_json.is_object()) {
                continue;
            }
            // The external-spool holder is not a CFS bay. Counting it renders a
            // phantom extra slot on a 4-bay unit.
            if (helix::json_util::safe_bool(slot_json, "external", false)) {
                continue;
            }

            SlotInfo slot;
            // Index by VECTOR POSITION, not the payload's `index`. Downstream
            // slot-widget creation walks this vector and treats position as the
            // bay, so a sparse or out-of-order payload must not leave holes. On
            // every payload seen so far the two agree; warn if they ever stop,
            // because a silent relabel would put a spool on the wrong bay.
            slot.slot_index = static_cast<int>(unit.slots.size());
            slot.global_index = slot.slot_index;
            if (int reported = helix::json_util::safe_int(slot_json, "index", slot.slot_index);
                reported != slot.slot_index) {
                spdlog::warn("[AMS CFS] Flat slot reports index {} at position {} — "
                             "using position; check for sparse or reordered slots",
                             reported, slot.slot_index);
            }
            // mapped_tool comes from the sync pass at the end of this function,
            // as in the stock parse — one writer for both directions. The flat
            // schema states no map of its own, so that pass hands every lane
            // the 1:1 default this line used to write.

            // The literal string "None" means ABSENT on every one of these
            // fields, and it is not a chosen value: the module builds each
            // profile entry with `str(value.get(key, ""))`, so a key that is
            // present but JSON null stringifies to Python's "None" rather than
            // falling back to the "" default. Every text field can therefore
            // arrive as "None", not just brand — surfacing it would put a spool
            // named "None" made of "None" on screen.
            auto text_field = [&slot_json](const char* key) -> std::string {
                std::string v = helix::json_util::safe_string(slot_json, key);
                return v == "None" ? std::string{} : v;
            };
            slot.material = text_field("material");
            slot.brand = text_field("brand");
            slot.spool_name = text_field("name");
            // Colors need no such guard: "None" is not six hex digits, so
            // parse_flat_slot_color already falls back to the default.
            slot.color_rgb =
                parse_flat_slot_color(helix::json_util::safe_string(slot_json, "color"));

            if (auto temp_it = material_temps.find(slot.material);
                temp_it != material_temps.end()) {
                slot.nozzle_temp_min = temp_it->second;
                slot.nozzle_temp_max = temp_it->second;
            }

            // Spoolman handles ship as null until the user links a spool.
            slot.spoolman_id = helix::json_util::safe_int(slot_json, "spoolman_id", 0);

            const bool present = helix::json_util::safe_bool(slot_json, "present", false);
            const bool loaded = helix::json_util::safe_bool(slot_json, "loaded", false);
            slot.status =
                loaded ? SlotStatus::LOADED : (present ? SlotStatus::AVAILABLE : SlotStatus::EMPTY);

            unit.slots.push_back(std::move(slot));
        }
    }

    unit.slot_count = static_cast<int>(unit.slots.size());
    info.total_slots = unit.slot_count;

    // loaded_slot is -1 when nothing is loaded. It indexes the same slots[]
    // array, so it needs no translation — but it can name the external entry,
    // which is not in our vector, hence the bounds check.
    int loaded_slot = helix::json_util::safe_int(box_json, "loaded_slot", -1);
    if (loaded_slot >= 0 && loaded_slot < unit.slot_count) {
        info.current_slot = loaded_slot;
        info.current_tool = loaded_slot;
    }

    info.units.push_back(std::move(unit));

    // Same single-writer pass as the stock parse. The flat payload carries no
    // `map`, so tool_to_slot_map is empty here and identity_fallback supplies
    // the 1:1 default in BOTH directions rather than in one of them.
    sync_tool_map_from_forward(info, /*identity_fallback=*/true);

    return info;
}

// Canonicalize per-slot RFID data into a fingerprint string used by
// check_hardware_event_clear. CFS exposes material_type and color_value as
// per-slot arrays; combining the raw strings (before strip_code / parse_color
// normalize sentinels away) gives a reliable "spool fingerprint". Sentinel
// values "-1" / "None" / empty produce an empty fingerprint string, which the
// helper treats as "no tag / unread".
//
// The fingerprint is intentionally stable across the same spool: CFS rewrites
// both fields from a server-side RFID lookup, so the same physical tag yields
// the same material_type code and the same color_value. A user swapping to a
// different spool changes at least one of those fields. CFS does NOT expose a
// dedicated CARD_UID field — this composite is the documented surrogate.
//
// color_value is NOT firmware-exclusive: push_slot_color_to_firmware writes it
// via BOX_MODIFY_TN_DATA so the stock LCD shows the user's chosen color. That
// write comes back around as a fingerprint change on a later poll, which reads
// identically to a physical swap. push_ therefore registers the expected
// post-write fingerprint with rfid_tracker_ (SlotFingerprintTracker::expect),
// and check_hardware_event_clear classifies the echo as OwnWriteEcho rather
// than a swap. material_type has no write path — nothing in the UI can set a
// raw CFS material code.
static std::string build_cfs_slot_uid(const nlohmann::json& unit_json, int local_index) {
    auto pick = [&](const char* field) -> std::string {
        if (!unit_json.contains(field) || !unit_json[field].is_array())
            return "";
        const auto& arr = unit_json[field];
        if (local_index < 0 || local_index >= static_cast<int>(arr.size()))
            return "";
        if (!arr[local_index].is_string())
            return "";
        auto s = arr[local_index].get<std::string>();
        // Treat sentinels as absent — otherwise a slot empty for two consecutive
        // parses would look like a "stable" UID baseline and the very next real
        // tag read would be flagged as a swap.
        if (s == "-1" || s == "None" || s.empty())
            return "";
        return s;
    };

    std::string mat = pick("material_type");
    std::string color = pick("color_value");
    if (mat.empty() && color.empty())
        return "";
    return mat + "|" + color;
}

// --- handle_status_update ---

void AmsBackendCfs::handle_status_update(const nlohmann::json& notification) {
    // notify_status_update format: {"method": "notify_status_update", "params": [{...}, timestamp]}
    if (!notification.contains("params") || !notification["params"].is_array() ||
        notification["params"].empty()) {
        return;
    }

    const auto& params = notification["params"][0];
    if (!params.is_object()) {
        return;
    }

    bool changed = false;

    // Drop the previous frame's derived LOADED stamp before anything below
    // reads or rebuilds the slot vector, so the override/clear/mirror pass sees
    // firmware truth. apply_seated_slot_stamp_locked() at the bottom re-derives
    // it once both the box branch and the toolhead-sensor branch have run.
    {
        std::lock_guard<std::mutex> lock(mutex_);
        clear_seated_slot_stamp_locked();
    }

    if (params.contains("box") && params["box"].is_object()) {
        const auto& box = params["box"];
        spdlog::debug("[AMS CFS] Received box data with {} keys", box.size());

        // Log box.filament_useup transitions with box-local context. Decoded
        // (2026-06-18, live K2 runout->reload): runout / path-empty signal —
        // 1 when no filament at the box gate (pre-load, runout), 0 when loaded
        // and feeding. Reads only the notification's own fields — no
        // system_info_ access, so no lock needed here. No UI effect yet.
        if (box.contains("filament_useup") && box["filament_useup"].is_number()) {
            int useup = box["filament_useup"].get<int>();
            if (useup != last_filament_useup_) {
                // safe_int, not .value(): spdlog evaluates its arguments before it
                // consults the log level, so a null or wrong-typed "filament"/
                // "auto_refill"/"enable" would throw type_error.302 out of
                // handle_status_update in a release build too, not just under -vv.
                // NB these are the TOP-LEVEL box fields, documented as ints — not
                // the same-named per-unit "filament" letter, which is a string.
                spdlog::debug("[AMS CFS] filament_useup {} -> {} (box.filament={}, "
                              "auto_refill={}, enable={})",
                              last_filament_useup_, useup,
                              helix::json_util::safe_int(box, "filament", -1),
                              helix::json_util::safe_int(box, "auto_refill", -1),
                              helix::json_util::safe_int(box, "enable", -1));
                last_filament_useup_ = useup;
            }
        }

        // Distinguish meaningful updates from noise (e.g., just measuring_wheel).
        // Full updates have "filament"/"map". Unit updates have "T1"/"T2"/etc.
        bool has_top_level = box.contains("filament") || box.contains("map");
        bool has_unit_data =
            box.contains("T1") || box.contains("T2") || box.contains("T3") || box.contains("T4");
        // Flat schema: a `slots` array is the payload that carries everything —
        // there is no top-level/per-unit split to reason about, so its presence
        // alone marks a full update.
        const bool is_flat = detect_schema(box) == CfsSchema::Flat;
        bool is_full_update = has_top_level || has_unit_data || is_flat;

        if (is_flat && schema_ != CfsSchema::Flat) {
            schema_ = CfsSchema::Flat;
            // Select the command dialect from the explicit API version, not by
            // assuming every `slots[]` payload implements the same commands.
            if (detect_fork_dialect(box)) {
                macro_variant_ = CfsMacroVariant::Fork;
                spdlog::info("[AMS CFS] Flat box schema + fork dialect detected "
                             "(community box.py, API v{}) — Fork control enabled",
                             helix::json_util::safe_int(box, "api_version", 0));
            } else {
                spdlog::warn("[AMS CFS] Flat box schema without a supported API version — "
                             "slot display active, control paths disabled (no verified "
                             "command dialect)");
            }
        }

        if (is_full_update) {
            auto new_info = parse_box_status(box);

            // Firmware-sourced mapping tick. box.map is what the CFS itself
            // reports — verified on a live K2: BOX_MODIFY_TN T1A=T1B echoed back
            // as a single-key delta in ~0.7s. This is what lets a remap restore
            // be confirmed against the box rather than against the optimistic
            // write set_tool_mapping() makes before sending (#1270). Bumped here
            // rather than inside parse_box_status because that parser is static.
            if (box.contains("map") && box["map"].is_object()) {
                ++firmware_map_generation_;
            }

            // Build observed per-slot RFID fingerprints for every unit present
            // in this notification. Slots that weren't included stay empty
            // (observed_uids stays at default ""), and empty-UID observations
            // are a no-op inside check_hardware_event_clear (no baseline
            // update, no clear) — exactly the behavior we want for incremental
            // updates that only touch a subset of units.
            std::unordered_map<int, std::string> observed_uids;
            for (int n = 1; n <= 4; ++n) {
                std::string key = "T" + std::to_string(n);
                if (!box.contains(key) || !box[key].is_object())
                    continue;
                const auto& unit_json = box[key];
                // safe_string for the same reason as the parse_box_status unit
                // loop: a null/wrong-typed `state` must degrade to "disconnected",
                // not throw out of handle_status_update.
                std::string state = helix::json_util::safe_string(unit_json, "state", "None");
                if (state == "None" || state == "-1")
                    continue;
                for (int i = 0; i < 4; ++i) {
                    int global_idx = (n - 1) * 4 + i;
                    observed_uids[global_idx] = build_cfs_slot_uid(unit_json, i);
                }
            }

            std::lock_guard<std::mutex> lock(mutex_);

            if (!new_info.units.empty()) {
                system_info_.units = std::move(new_info.units);
                system_info_.total_slots = new_info.total_slots;
                system_info_.endless_spool_enabled = new_info.endless_spool_enabled;
                system_info_.tool_to_slot_map = std::move(new_info.tool_to_slot_map);
            }

            // Deliberately do NOT touch filament_loaded here. box.filament is a
            // selection index, not a loaded flag (see parse_box_status). The
            // toolhead-sensor branch below is the sole writer of
            // filament_loaded — a box update lacking the sensor param must not
            // clobber the sensor-derived value.

            // Update runout flag only when the field was actually present.
            // Stock spells it filament_useup; the flat schema spells it runout
            // (JSON null while idle, a descriptor once tripped).
            if (box.contains("filament_useup") || (is_flat && box.contains("runout"))) {
                system_info_.filament_runout = new_info.filament_runout;
            }

            // Active slot from T{n}.filament field ("A"/"B"/"C"/"D"). When the
            // notification carried per-unit lane data but no unit reports an
            // active lane (current_slot < 0) and we're not mid-load, clear the
            // active selection. Driven solely by the per-unit T{n}.filament
            // letter — not box.filament, which is a stale selection index, not
            // a loaded flag. Gate on has_unit_data so a partial top-level-only
            // update (e.g. box:{filament:N} during a tool change) can't clobber
            // a still-valid active slot.
            if (new_info.current_slot >= 0) {
                system_info_.current_slot = new_info.current_slot;
                system_info_.current_tool = new_info.current_tool;
            } else if ((has_unit_data || is_flat) && system_info_.action != AmsAction::LOADING) {
                system_info_.current_slot = -1;
                system_info_.current_tool = -1;
            }

            // Override integration convergence point. Firmware-sourced fields
            // are now written to system_info_.units; run the hardware-event
            // check FIRST (so it sees firmware truth, not override-masked
            // data) and apply_overrides AFTER (so the final SlotInfo visible
            // via get_slot_info reflects user edits).
            for (auto& unit : system_info_.units) {
                for (size_t j = 0; j < unit.slots.size(); ++j) {
                    auto& slot = unit.slots[j];
                    int global_idx = unit.first_slot_global_index + static_cast<int>(j);

                    auto uid_it = observed_uids.find(global_idx);
                    const std::string& observed_uid =
                        (uid_it != observed_uids.end()) ? uid_it->second : std::string{};

                    // Both clear paths run BEFORE apply_overrides so a clear's
                    // field reset isn't masked by a stale override layer.
                    bool cleared = check_hardware_event_clear(slot, global_idx, observed_uid);
                    cleared |= clear_stale_override_on_removal_locked(slot, global_idx);

                    // Mirror firmware-truth color/material into lane_data so
                    // OrcaSlicer's MoonrakerPrinterAgent sees the spool. Runs
                    // BEFORE apply_overrides so the values reflect firmware,
                    // not the override-masked view. FillUnsetOnly: CFS user
                    // edits don't reach firmware, so we must not let firmware
                    // overwrite them — see mirror_firmware_to_lane_data docs.
                    //
                    // Skipped on a parse that cleared: a clear fires clear_async
                    // (DELETE) and the mirror fires save_async (POST) against
                    // the SAME lane_data key. Both are async and independently
                    // ordered, so issuing them together is a write race whose
                    // outcome depends on which reply Moonraker processes last —
                    // a DELETE landing second silently drops the record we just
                    // published. The next poll republishes from firmware truth
                    // with the delete already settled.
                    if (!cleared) {
                        helix::ams::mirror_firmware_to_lane_data(
                            override_store_.get(), overrides_, global_idx, slot.color_rgb,
                            slot.material, slot.status == SlotStatus::AVAILABLE,
                            helix::ams::MirrorPolicy::FillUnsetOnly, backend_log_tag());
                    }
                    apply_overrides(slot, global_idx);
                }
            }
        }
        // Partial updates (measuring_wheel, etc.): skip — don't touch state
        changed = true;
    }

    if (params.contains("filament_switch_sensor filament_sensor")) {
        const auto& sensor = params["filament_switch_sensor filament_sensor"];
        // filament_detected: Klipper publishes this as null until the sensor
        // takes its first reading. Use .find() + is_boolean() (per [L087])
        // rather than a bare get<bool>(), which throws type_error.302 on that
        // null — and because the throw escapes into UpdateQueue's catch, it
        // would take the extruder-temp and motor_control blocks below down
        // with it and skip the EVENT_STATE_CHANGED emit, freezing the UI on
        // stale AMS state. A null means "no reading", not "no filament", so
        // there is no safe default here: skip and keep the previous value.
        auto fd_it = sensor.find("filament_detected");
        if (fd_it != sensor.end() && fd_it->is_boolean()) {
            std::lock_guard<std::mutex> lock(mutex_);
            bool detected = fd_it->get<bool>();
            system_info_.filament_loaded = detected;

            // The filament_switch_sensor sits at the toolhead extruder. It
            // trips at the END of CR_BOX_EXTRUDE — long before the load
            // sequence's CR_BOX_WASTE + CR_BOX_FLUSH (~109 mm @ 240 °C, ~3
            // min) actually finishes. Don't flip `action` here: completion
            // semantics live in `dispatch_action_script`'s gcode-script
            // success callback, which fires when Klipper drains the *entire*
            // script. We just mirror the live filament-present flag.

            // Drive phase synthesis off the transition (Task #2).
            if (detected != last_filament_detected_) {
                on_filament_transition_locked(detected);
            }
            last_filament_detected_ = detected;
        }
        changed = true;
    }

    if (params.contains("extruder")) {
        const auto& extr = params["extruder"];
        bool action_transitioned = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (extr.contains("target") && extr["target"].is_number()) {
                last_extruder_target_deci_ =
                    helix::units::to_decidegrees(extr["target"].get<double>());
            }
            if (extr.contains("temperature") && extr["temperature"].is_number()) {
                last_extruder_temp_deci_ =
                    helix::units::to_decidegrees(extr["temperature"].get<double>());
            }
            AmsAction before = system_info_.action;
            on_extruder_temp_change_locked(last_extruder_temp_deci_, last_extruder_target_deci_);
            action_transitioned = (system_info_.action != before);
        }
        // Extruder telemetry is high-frequency; only emit when action
        // actually moved, otherwise we'd thrash the UI on every temp tick.
        if (action_transitioned) {
            changed = true;
        }
    }

    if (params.contains("motor_control")) {
        const auto& motor = params["motor_control"];
        // Same null hazard as filament_detected above — .find() + is_boolean()
        // so a null reading leaves motor_ready_ at its previous value instead
        // of throwing out of the handler.
        auto mr_it = motor.find("motor_ready");
        if (mr_it != motor.end() && mr_it->is_boolean()) {
            std::lock_guard<std::mutex> lock(mutex_);
            motor_ready_ = mr_it->get<bool>();
        }
        changed = true;
    }

    // The lane letter and the toolhead switch arrive on independent branches
    // above (and often on independent notifications), so the seated bay can
    // only be resolved once both have been applied.
    {
        std::lock_guard<std::mutex> lock(mutex_);
        apply_seated_slot_stamp_locked();
    }

    if (changed) {
        emit_event(EVENT_STATE_CHANGED);
    }
}

void AmsBackendCfs::clear_seated_slot_stamp_locked() {
    // Caller holds mutex_.
    if (seated_stamp_slot_ < 0) {
        return;
    }

    SlotInfo* slot = system_info_.get_slot_global(seated_stamp_slot_);
    // A rebuilt slot vector has already written firmware truth here, so the
    // saved status is stale — only restore over a stamp we can still see.
    if (slot != nullptr && slot->status == SlotStatus::LOADED) {
        slot->status = seated_stamp_prev_;
    }

    seated_stamp_slot_ = -1;
    seated_stamp_prev_ = SlotStatus::UNKNOWN;
}

void AmsBackendCfs::apply_seated_slot_stamp_locked() {
    // Caller holds mutex_.
    clear_seated_slot_stamp_locked();

    // Both halves are required. The T{n}.filament letter alone is a lane
    // SELECTION — CFS reports it through a load that has not yet reached the
    // extruder, and on K1 for a merely cassette-staged slot (#968) — so only
    // the toolhead filament_switch_sensor makes it a seat.
    if (!system_info_.filament_loaded || system_info_.current_slot < 0) {
        return;
    }

    SlotInfo* slot = system_info_.get_slot_global(system_info_.current_slot);
    if (slot == nullptr) {
        return;
    }

    seated_stamp_slot_ = system_info_.current_slot;
    seated_stamp_prev_ = slot->status;
    slot->status = SlotStatus::LOADED;
}

// --- State queries ---

AmsSystemInfo AmsBackendCfs::get_system_info() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return system_info_;
}

SlotInfo AmsBackendCfs::get_slot_info(int slot_index) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto* slot = system_info_.get_slot_global(slot_index);
    if (slot) {
        return *slot;
    }
    return SlotInfo{};
}

// --- Path segments ---

PathSegment AmsBackendCfs::get_filament_segment() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return system_info_.filament_loaded ? PathSegment::NOZZLE : PathSegment::NONE;
}

PathSegment AmsBackendCfs::get_slot_filament_segment(int slot_index) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto* slot = system_info_.get_slot_global(slot_index);
    if (!slot) {
        return PathSegment::NONE;
    }

    bool is_active = (system_info_.current_slot == slot_index);

    switch (slot->status) {
    case SlotStatus::AVAILABLE:
    case SlotStatus::FROM_BUFFER:
        // Active slot with filament loaded at nozzle = full path
        if (is_active && system_info_.filament_loaded) {
            return PathSegment::NOZZLE;
        }
        // Active slot during loading = show at hub (in transit)
        if (is_active && system_info_.action == AmsAction::LOADING) {
            return PathSegment::HUB;
        }
        return PathSegment::HUB;
    case SlotStatus::LOADED:
        return PathSegment::NOZZLE;
    default:
        return PathSegment::NONE;
    }
}

PathSegment AmsBackendCfs::infer_error_segment() const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& alert : system_info_.alerts) {
        if (alert.level == AmsAlertLevel::SLOT) {
            return PathSegment::LANE;
        }
        if (alert.level == AmsAlertLevel::SYSTEM || alert.level == AmsAlertLevel::UNIT) {
            return PathSegment::HUB;
        }
    }
    return PathSegment::NONE;
}

// --- Operations ---

AmsError AmsBackendCfs::reject_if_flat_schema(const char* operation) const {
    // A flat box paired with the Fork dialect is fully driveable — the command
    // set is verified against the module's own registration table. Only a flat
    // box we cannot identify stays gated: emitting the stock CR_BOX_*/BOX_*
    // sequences at an unknown module sends commands it may not define.
    if (schema_ != CfsSchema::Flat || macro_variant_ == CfsMacroVariant::Fork) {
        return AmsErrorHelper::success();
    }
    spdlog::warn("[AMS CFS] {} refused — unrecognized flat box module, no verified dialect",
                 operation);
    return AmsErrorHelper::not_supported(std::string(operation) + " on this firmware's CFS module");
}

AmsError AmsBackendCfs::do_load_filament(int slot_index) {
    auto err = reject_if_flat_schema("Load");
    if (err.result != AmsResult::SUCCESS)
        return err;
    auto gcode = load_gcode(slot_index, macro_variant_);
    if (gcode.empty()) {
        return AmsErrorHelper::invalid_slot(slot_index, 15);
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        system_info_.action = AmsAction::LOADING;
        system_info_.current_slot = slot_index;
        begin_phase_tracking();
        apply_synthesized_action_locked();
        // Optimistic current_slot move with no status frame behind it — keep
        // the stamp on the same bay the aggregate now names.
        apply_seated_slot_stamp_locked();
    }
    return dispatch_action_script(std::move(gcode));
}

AmsError AmsBackendCfs::do_unload_filament(int) {
    auto err = reject_if_flat_schema("Unload");
    if (err.result != AmsResult::SUCCESS)
        return err;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        system_info_.action = AmsAction::UNLOADING;
        begin_phase_tracking();
        apply_synthesized_action_locked();
    }
    return dispatch_action_script(unload_gcode(macro_variant_));
}

AmsError AmsBackendCfs::do_select_slot(int) {
    return AmsErrorHelper::not_supported("CFS loads directly");
}

AmsError AmsBackendCfs::do_change_tool(int tool) {
    auto err = reject_if_flat_schema("Tool change");
    if (err.result != AmsResult::SUCCESS)
        return err;

    bool needs_unload = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        // Cut-first decision centralized in needs_unload_before_load(): on K1 CFS
        // current_slot reports a *preloaded* (cassette-staged) slot with the
        // nozzle still empty, so the K1 override keys on filament_loaded only
        // (avoids the "hallucinated cut on an empty nozzle" the reporter saw,
        // #968). K2 retains the filament_loaded || current_slot >= 0 behavior.
        //
        // `tool` doubles as the target slot: CFS bays map 1:1 to tools. Safe
        // under mutex_ — CFS does not override get_unit_topology(), so the
        // base's per-lane arm reaches only the inline get_topology() constant.
        needs_unload = needs_unload_before_load(system_info_, tool);
    }

    // Validate gcode before mutating state
    std::string gcode =
        needs_unload ? swap_gcode(tool, macro_variant_) : load_gcode(tool, macro_variant_);
    if (gcode.empty()) {
        return AmsErrorHelper::invalid_slot(tool, 15);
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        system_info_.action = AmsAction::LOADING;
        system_info_.current_slot = tool;
        begin_phase_tracking();
        apply_synthesized_action_locked();
        // Same as load_filament: the aggregate moved without a status frame.
        apply_seated_slot_stamp_locked();
    }
    return dispatch_action_script(std::move(gcode));
}

AmsError AmsBackendCfs::reset() {
    auto err = check_preconditions();
    if (err.result != AmsResult::SUCCESS)
        return err;
    return execute_gcode(reset_gcode());
}

AmsError AmsBackendCfs::recover() {
    auto err = check_preconditions();
    if (err.result != AmsResult::SUCCESS)
        return err;
    return execute_gcode(recover_gcode());
}

AmsError AmsBackendCfs::cancel() {
    auto err = check_preconditions();
    if (err.result != AmsResult::SUCCESS)
        return err;
    return execute_gcode("CANCEL_PRINT");
}

// --- set_slot_info ---

AmsError AmsBackendCfs::set_slot_info(int slot_index, const SlotInfo& info, bool persist) {
    {
        std::lock_guard<std::mutex> lock(mutex_);

        // Find the unit and local slot for this global index
        SlotInfo* target = nullptr;
        for (auto& unit : system_info_.units) {
            int first = unit.first_slot_global_index;
            int last = first + static_cast<int>(unit.slots.size()) - 1;
            if (slot_index >= first && slot_index <= last) {
                int local = slot_index - first;
                target = &unit.slots[local];
                break;
            }
        }

        if (!target) {
            return AmsErrorHelper::invalid_slot(slot_index, system_info_.total_slots - 1);
        }

        // Update in-memory slot state so get_slot_info returns the edit
        // immediately — covers every SlotInfo field the caller may have set,
        // including persist=false previews that must survive until the next
        // firmware parse.
        target->color_rgb = info.color_rgb;
        target->color_name = info.color_name;
        target->material = info.material;
        target->brand = info.brand;
        // Carry the catalog product identity through preview writes too — a
        // persist=false preview that dropped it would make the editor snap
        // back to a different variant on the next get_slot_info().
        target->catalog_id = info.catalog_id;
        target->product_name = info.product_name;
        target->spool_name = info.spool_name;
        target->spoolman_id = info.spoolman_id;
        target->spoolman_vendor_id = info.spoolman_vendor_id;
        target->remaining_weight_g = info.remaining_weight_g;
        target->total_weight_g = info.total_weight_g;

        // For persist=true, stage the override into overrides_ so
        // apply_overrides re-applies the new values on every subsequent parse.
        // For persist=false we explicitly do NOT touch overrides_ — preview
        // edits are in-memory only and will be overwritten by the next
        // firmware parse (expected preview contract).
        //
        // NOTE on self-wipe: CFS's hardware-event check is RFID-fingerprint-
        // based (material_type + color_value from firmware), and one half of
        // that fingerprint IS user-writable — push_slot_color_to_firmware
        // (below, persist path only) rewrites color_value via
        // BOX_MODIFY_TN_DATA. Firmware echoes that write back on a later poll,
        // where it is indistinguishable from a physical spool swap and would
        // erase the override we are staging right here.
        //
        // The self-wipe guard lives in push_slot_color_to_firmware, which
        // registers the expected post-write fingerprint with rfid_tracker_
        // before dispatching the gcode. material_type has no write path, so
        // that half of the fingerprint remains pure firmware truth.
        if (persist) {
            helix::ams::FilamentSlotOverride ovr;
            ovr.brand = info.brand;
            ovr.spool_name = info.spool_name;
            ovr.spoolman_id = info.spoolman_id;
            ovr.spoolman_vendor_id = info.spoolman_vendor_id;
            ovr.remaining_weight_g = info.remaining_weight_g;
            ovr.total_weight_g = info.total_weight_g;
            ovr.color_rgb = info.color_rgb;
            ovr.color_set = true; // a user-edit always records a color, even pure black (#000000)
            ovr.color_name = info.color_name;
            ovr.material = info.material;
            // Catalog product identity. Persisted so a reopen can restore the
            // EXACT product rather than the alphabetically-first variant of the
            // same vendor+material. Never auto-mirrored (firmware has no notion
            // of a catalog product), so no user-lock flag is needed: a non-empty
            // value can only have come from a user pick.
            ovr.catalog_id = info.catalog_id;
            ovr.product_name = info.product_name;
            // User-lock: CFS uses FillUnsetOnly so the locks are
            // belt-and-suspenders here, but they keep the on-disk schema
            // consistent across backends and protect against a future
            // policy change. See #965 for the motivating bug.
            ovr.user_locked_color = true;
            ovr.user_locked_material = !info.material.empty();
            // SlotInfo carries the user's edit OR the bound Spoolman spool's
            // filament profile; the material-DB fallback for fields left at 0
            // is applied at emit time inside resolved_temps(). Centralized in
            // the helper so the four AMS backends stay in sync.
            helix::ams::populate_temps_from_slot_info(ovr, info);
            // updated_at left default — save_async stamps a fresh value.
            overrides_[slot_index] = ovr;
        }
    }

    spdlog::info("[AMS CFS] Updated slot {} info (persist={}): {} {}", slot_index, persist,
                 info.material, info.color_name);

    if (persist && override_store_) {
        // Re-read from overrides_ under the lock to pick up the staged copy.
        helix::ams::FilamentSlotOverride ovr_to_save;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = overrides_.find(slot_index);
            if (it != overrides_.end()) {
                ovr_to_save = it->second;
            }
        }
        // Capture by value — save_async's MR callback may fire long after
        // this returns (MR tracker ~60s timeout). Do NOT capture `this`:
        // the backend may outlive its store, but the store will outlive
        // the scheduled save by design.
        const std::string tag = backend_log_tag();
        override_store_->save_async(
            slot_index, ovr_to_save, [tag, slot_index](bool success, const std::string& err) {
                if (!success) {
                    spdlog::warn("{} Override persist failed for slot {}: {}", tag, slot_index,
                                 err);
                }
            });

        // Push the user's color back to firmware so the K2's stock LCD and
        // any other Moonraker-DB readers see the same value. Color-only —
        // material codes are CFS-internal and we don't have a complete
        // reverse-map. See push_slot_color_to_firmware docs for the discovered
        // BOX_MODIFY_TN_DATA gcode syntax.
        push_slot_color_to_firmware(slot_index, info.color_rgb);
    }

    emit_event(EVENT_SLOT_CHANGED, std::to_string(slot_index));
    return AmsErrorHelper::success();
}

void AmsBackendCfs::push_slot_color_to_firmware(int global_index, uint32_t color_rgb) {
    // Validate slot index BEFORE formatting the gcode — invalid args trigger
    // an unhandled TypeError in box_wrapper that Klipper escalates to
    // invoke_shutdown. Better to silently no-op than to crash the printer.
    //
    // No color-value validation here on purpose: pure black (0x000000) is a
    // legitimate user choice and we don't want to silently drop it. The
    // caller (set_slot_info, which sets color_set=true on the override) is
    // responsible for only invoking this when a real color was chosen.
    constexpr int CFS_MAX_SLOTS = 16; // 4 units × 4 slots
    if (global_index < 0 || global_index >= CFS_MAX_SLOTS) {
        spdlog::debug("{} push_slot_color_to_firmware: skipping invalid slot {}", backend_log_tag(),
                      global_index);
        return;
    }

    // The Fork module defines no BOX_MODIFY_TN_DATA. `_BOX_SLOT_SET` requires
    // a material; explicit clears route through clear_box_slot_profile().
    if (macro_variant_ == CfsMacroVariant::Fork) {
        std::string material;
        std::string brand;
        std::string name;
        int spoolman_id = 0;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (const auto& unit : system_info_.units) {
                for (const auto& slot : unit.slots) {
                    if (slot.global_index == global_index) {
                        material = slot.material;
                        brand = slot.brand;
                        name = slot.spool_name;
                        spoolman_id = slot.spoolman_id;
                        break;
                    }
                }
            }
        }
        std::string gcode =
            slot_set_gcode(global_index, material, color_rgb, brand, name, spoolman_id);
        if (gcode.empty()) {
            spdlog::debug("{} slot-set skipped for slot {}", backend_log_tag(), global_index);
            return;
        }
        execute_gcode(gcode);
        return;
    }

    // A flat box we could not identify: no verified write command at all.
    if (schema_ == CfsSchema::Flat) {
        spdlog::debug("{} push_slot_color_to_firmware: unknown flat module — local override only",
                      backend_log_tag());
        return;
    }

    const int unit = (global_index / 4) + 1;           // 1..4
    const char slot_letter = 'A' + (global_index % 4); // A..D
    char data[16];
    // CFS color_value format observed in tn_data.json: 7 hex chars, leading
    // nibble appears to be alpha (always 0 in stock data). RGB is the trailing
    // 6 hex digits. Match the firmware's own "0RRGGBB" format exactly.
    std::snprintf(data, sizeof(data), "0%06X", color_rgb & 0xFFFFFFu);

    char gcode[96];
    std::snprintf(gcode, sizeof(gcode),
                  "BOX_MODIFY_TN_DATA ADDR=%d NUM=%c PART=color_value DATA=%s", unit, slot_letter,
                  data);

    // Self-wipe guard. Once firmware applies this write it reports the new
    // color_value back to us, changing the RFID fingerprint that
    // check_hardware_event_clear watches — which on its own reads exactly like
    // a physical spool swap and erases the override the user just created.
    //
    // Register the fingerprint we expect to see once the write lands: the
    // CURRENT material_type (this gcode does not touch it) paired with the
    // color we're about to send. We deliberately do NOT overwrite the baseline
    // outright: the gcode is queued asynchronously behind whatever else Klipper
    // is running, so firmware keeps reporting the OLD color for an unknown
    // number of polls first. Those polls must stay Unchanged, and only the
    // eventual echo may consume the expectation.
    //
    // The expectation is single-shot and any non-matching change consumes it
    // too, so a genuine spool swap that happens while this write is in flight
    // is still detected and swap detection is never left permanently blinded.
    //
    // With no baseline yet there is nothing to build an expectation from — and
    // none is needed: the first observation for a slot is always a baseline and
    // never fires a clear.
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (auto base = rfid_tracker_.baseline(global_index)) {
            const auto bar = base->find('|');
            const std::string material = (bar == std::string::npos) ? *base : base->substr(0, bar);
            rfid_tracker_.expect(global_index, material + "|" + data);
        }
    }

    auto err = execute_gcode(gcode);
    if (err.result != AmsResult::SUCCESS) {
        // Non-fatal — the override is already in lane_data so user data isn't
        // lost; only the firmware-side LCD won't reflect this edit.
        //
        // Drop the expectation: no echo is coming, so the next fingerprint
        // change is genuinely external and must be treated as a swap.
        {
            std::lock_guard<std::mutex> lock(mutex_);
            rfid_tracker_.forget_expected(global_index);
        }
        spdlog::warn("{} push_slot_color_to_firmware: gcode dispatch failed for slot {}: {}",
                     backend_log_tag(), global_index, err.technical_msg);
    }
}

AmsError AmsBackendCfs::set_tool_mapping(int tool_number, int slot_index) {
    // CFS exposes per-print tool→slot remap via the BOX_MODIFY_TN gcode (format
    // observed in box_wrapper.cpython-39.so: "BOX_MODIFY_TN %s=%s"). Both sides
    // use the TNN notation (T1A..T4D) that matches the box.map JSON keys/values
    // returned by Moonraker.
    //
    // Example: set_tool_mapping(0, 5) sends "BOX_MODIFY_TN T1A=T2B" — when the
    // slicer emits T0/T1A, the CFS routes from physical slot T2B (index 5).
    constexpr int CFS_MAX_SLOTS = 16; // 4 units × 4 slots
    if (tool_number < 0 || tool_number >= CFS_MAX_SLOTS) {
        return AmsError(AmsResult::INVALID_TOOL,
                        "Tool " + std::to_string(tool_number) + " out of range",
                        "Invalid tool number", "");
    }
    if (slot_index < 0 || slot_index >= CFS_MAX_SLOTS) {
        return AmsErrorHelper::invalid_slot(slot_index, CFS_MAX_SLOTS - 1);
    }

    std::string tool_tnn = CfsMaterialDb::slot_to_tnn(tool_number);
    std::string slot_tnn = CfsMaterialDb::slot_to_tnn(slot_index);
    if (tool_tnn.empty() || slot_tnn.empty()) {
        return AmsError(AmsResult::INVALID_TOOL,
                        "Failed to encode TNN for tool=" + std::to_string(tool_number) +
                            " slot=" + std::to_string(slot_index),
                        "Invalid tool/slot", "");
    }

    // Optimistic local update so get_tool_mapping() reflects the new mapping
    // immediately for UI/restore-snapshot reads. The next box-status websocket
    // update will re-confirm (or correct, if Klipper rejected the BOX_MODIFY_TN
    // for some reason) — firmware remains the source of truth.
    //
    // Both directions move together (assign_tool_slot), because the AMS panel's
    // lane badge reads mapped_tool while the filament panel's op buttons read
    // tool_to_slot_map. Writing the forward entry alone left the badge on the
    // lane the tool was moved AWAY from until the next box frame — and on K1,
    // where BOX_MODIFY_TN is known to no-op, no such frame ever arrives.
    //
    // Only lanes the current parse actually knows about can be recorded: a
    // forward entry naming a lane with no SlotInfo has no reverse counterpart
    // to pair with, and resolve_op_button_slot() would hand the filament panel
    // a slot index that get_slot_global() answers with nullptr. The command is
    // still dispatched — firmware is the authority, the box may have a unit we
    // have not parsed a frame for yet, and that frame is what will make the
    // mapping real in both directions.
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const int known_slots = system_info_.total_slots;
        if (slot_index < known_slots) {
            assign_tool_slot(system_info_, tool_number, slot_index);
        } else if (known_slots > 0) {
            spdlog::warn("[AMS CFS] Remap targets slot {} but only {} lanes are connected; "
                         "sending the command, not recording it locally — the next box frame "
                         "decides",
                         slot_index, known_slots);
        } else {
            spdlog::debug("[AMS CFS] Remap issued before the first box frame; local map "
                          "stays empty until one arrives");
        }
    }

    std::string cmd = "BOX_MODIFY_TN " + tool_tnn + "=" + slot_tnn;
    spdlog::info("[AMS CFS] Remapping tool {} -> slot {} ({})", tool_tnn, slot_tnn, cmd);
    if (macro_variant_ == CfsMacroVariant::K1) {
        // #968 Phase 5 — the reporter confirmed BOX_MODIFY_TN no-ops on the K1
        // official CFS upgrade firmware (the box's "current TN" state it sets is
        // ignored), and we have no confirmed working alternative. We still emit
        // it because it is harmless (no key61 "Unknown command"), but in-print
        // tool->slot remap may simply not take effect on K1. The optimistic
        // local update above keeps the UI consistent regardless.
        // TODO(#968): confirm a working K1 remap path on hardware (console test)
        // before advertising remap capability on K1 in get_tool_mapping_capabilities.
        spdlog::info("[AMS CFS] K1 firmware: in-print tool remap (BOX_MODIFY_TN) may be "
                     "unsupported; emitting anyway (harmless). Verify on hardware (#968).");
    }
    return execute_gcode(cmd);
}

AmsError AmsBackendCfs::enable_bypass() {
    return AmsErrorHelper::not_supported("CFS has no bypass");
}

AmsError AmsBackendCfs::disable_bypass() {
    return AmsErrorHelper::not_supported("CFS has no bypass");
}

// --- GCode helpers ---

namespace {

/// Wrap a CR_BOX_* operation with the K2 macro envelope.
///
/// Sequence (better-than-stock — see notes below):
///
///   1. SAVE_GCODE_STATE        — preserve caller coordinate mode + feedrate
///   2. BOX_SAVE_FAN            — suppress part-cooling during the op
///   3. BOX_GO_TO_EXTRUDE_POS   — toolhead over waste chute (X=133/Y=378)
///   4. BOX_MODE_WAIT           — wait for box state machine to settle
///                                before the active phase
///   5. <body>                  — CR_BOX_PRE_OPT, EXTRUDE/CUT/RETRUDE/etc.
///   6. BOX_NOZZLE_CLEAN        — (load/swap only) wipe on silicone pad
///                                at X=160-170, Y=374
///   7. BOX_RESTORE_FAN         — restore part-cooling state
///   8. BOX_MOVE_TO_SAFE_POS    — park at safe_pos (X=225/Y=345)
///   9. RESTORE_GCODE_STATE
///
/// Stock K2 BOX_LOAD_MATERIAL_HEATING calls BOX_SET_TEMP, which targets
/// Tn_extrude_temp (220°C). We deliberately omit it: if a user has PETG
/// loaded at 240°C and taps Load, BOX_SET_TEMP would cool the hotend
/// mid-op and the subsequent flush would underextrude. A cold extruder
/// instead surfaces a friendly "Extrude below minimum temp — pre-heat
/// first" modal via the key111 entry in CFS_ERROR_TABLE.
///
/// Klipper macros have no try/finally — if the body raises, steps 7-9
/// are skipped. Best-effort unwind (independent BOX_RESTORE_FAN +
/// RESTORE_GCODE_STATE scripts) lives in `dispatch_action_script`'s
/// on_error path.
///
/// `wipe_after` toggles step 6. Load and swap end with the nozzle full
/// of fresh filament — wipe required (observed K2 Plus 2026-05-23: T1
/// load drip-trailed across the bed). Unload ends with the nozzle empty
/// post-cut — wipe omitted to avoid pushing dribble back into the hotend.
std::string wrap_with_park_k2(const std::string& body, bool wipe_after) {
    std::string post_body = "\n";
    if (wipe_after) {
        post_body += "BOX_NOZZLE_CLEAN\n";
    }
    post_body += "BOX_RESTORE_FAN\n"
                 "BOX_MOVE_TO_SAFE_POS\n"
                 "RESTORE_GCODE_STATE NAME=helix_cfs_load";
    return "SAVE_GCODE_STATE NAME=helix_cfs_load\n"
           "BOX_SAVE_FAN\n"
           "BOX_GO_TO_EXTRUDE_POS\n"
           "BOX_MODE_WAIT\n" +
           body + post_body;
}

/// Wrap a BOX_* step body with the K1 official CFS macro envelope (#968).
///
/// The K1 firmware does not expose BOX_SAVE_FAN / BOX_RESTORE_FAN /
/// BOX_MODE_WAIT (verified absent in the public K1-Max box.cfg dump at
/// DieDutchman/K1-Max-KAMP-CFS-Fix, and from the #968 reporter's gcode/help
/// output). Every K1 load/swap/unload sequence shares the same envelope:
///
///   SAVE_GCODE_STATE NAME=helix_cfs_load
///   BOX_ERROR_CLEAR
///   BOX_CHECK_MATERIAL
///   <body>                       — the variant-specific BOX_* step list
///   BOX_MOVE_TO_SAFE_POS         — park
///   RESTORE_GCODE_STATE NAME=helix_cfs_load
///
/// `body` carries everything between CHECK_MATERIAL and the safe-park tail,
/// including each builder's own positioning (BOX_GO_TO_EXTRUDE_POS) and wipe
/// (BOX_NOZZLE_CLEAN) — load wipes after the feed, swap wipes before it, and
/// unload has neither, so those steps live in the body rather than the
/// envelope. The emitted string is byte-identical to the prior inline literals.
std::string wrap_with_envelope_k1(const std::string& body) {
    return "SAVE_GCODE_STATE NAME=helix_cfs_load\n"
           "BOX_ERROR_CLEAR\n"
           "BOX_CHECK_MATERIAL\n" +
           body +
           "\nBOX_MOVE_TO_SAFE_POS\n"
           "RESTORE_GCODE_STATE NAME=helix_cfs_load";
}

std::string wrap_with_park(CfsMacroVariant variant, const std::string& body, bool wipe_after) {
    // Only the K2 (CR_BOX_*) builders route through here — the K1 builders own
    // their wipe/positioning explicitly and call wrap_with_envelope_k1 directly.
    // The K1 branch (envelope has no wipe knob) is kept for completeness/safety.
    return variant == CfsMacroVariant::K1 ? wrap_with_envelope_k1(body)
                                          : wrap_with_park_k2(body, wipe_after);
}

} // namespace

bool AmsBackendCfs::detect_fork_dialect(const nlohmann::json& box_json) {
    return box_json.is_object() && helix::json_util::safe_int(box_json, "api_version", 0) == 1;
}

std::string AmsBackendCfs::slot_set_gcode(int global_slot_index, const std::string& material,
                                          uint32_t color_rgb, const std::string& brand,
                                          const std::string& name, int spoolman_id) {
    constexpr int CFS_MAX_SLOTS = 16; // 4 units × 4 slots
    if (global_slot_index < 0 || global_slot_index >= CFS_MAX_SLOTS) {
        spdlog::error("[AMS CFS] Invalid slot index for slot-set: {}", global_slot_index);
        return "";
    }
    // MATERIAL is required by cmd_slot_set — an empty one is a hard command
    // error, so emit nothing rather than a rejected write.
    if (material.empty()) {
        spdlog::debug("[AMS CFS] slot_set: no material for slot {} — skipping write",
                      global_slot_index);
        return "";
    }
    std::string upper = material;
    for (char& c : upper) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    char color[10];
    std::snprintf(color, sizeof(color), "#%06X", color_rgb & 0xFFFFFFu);
    return "_BOX_SLOT_SET SLOT=" + std::to_string(global_slot_index) +
           " MATERIAL=" + quote_gcode_param(upper) + " COLOR=" + quote_gcode_param(color) +
           " BRAND=" + quote_gcode_param(brand) + " NAME=" + quote_gcode_param(name) +
           " SPOOLMAN_ID=" + std::to_string(spoolman_id > 0 ? spoolman_id : -1);
}

std::string AmsBackendCfs::load_gcode(int idx, CfsMacroVariant variant) {
    if (variant == CfsMacroVariant::Fork) {
        // The Box T command owns the complete load or change operation.
        constexpr int CFS_MAX_SLOTS = 16;
        if (idx < 0 || idx >= CFS_MAX_SLOTS) {
            spdlog::error("[AMS CFS] Invalid slot index for load: {}", idx);
            return "";
        }
        return "T" + std::to_string(idx);
    }
    std::string tnn = CfsMaterialDb::slot_to_tnn(idx);
    if (tnn.empty()) {
        spdlog::error("[AMS CFS] Invalid slot index for load: {}", idx);
        return "";
    }
    if (variant == CfsMacroVariant::K1) {
        // K1 official CFS upgrade firmware — fresh load, nozzle empty. Mirrors
        // the firmware orchestrator BOX_LOAD_MATERIAL_WITHOUT_MATERIAL step
        // list with explicit TNN= (the box's bare-macro "current TN" set via
        // BOX_MODIFY_TN no-ops on K1, per the reporter):
        //   ERROR_CLEAR → CHECK_MATERIAL → EXTRUDE → EXTRUDER_EXTRUDE → FLUSH
        // BOX_EXTRUDER_EXTRUDE is the root-cause fix for "no auto-extrude after
        // load" (#968): the firmware's WITHOUT_MATERIAL chain drives the main
        // extruder after the cassette feed, and we previously omitted it.
        // Homing is handled upstream by dispatch_action_script; do NOT add
        // IF_NEED_HOME here. Envelope (wrap_with_envelope_k1) adds ERROR_CLEAR /
        // CHECK_MATERIAL / MOVE_TO_SAFE_POS; the body carries positioning, the
        // feed steps, and the post-feed wipe. All commands confirmed in box.cfg.
        return wrap_with_envelope_k1("BOX_GO_TO_EXTRUDE_POS\n"
                                     "BOX_EXTRUDE_MATERIAL TNN=" +
                                     tnn + "\nBOX_EXTRUDER_EXTRUDE TNN=" + tnn +
                                     "\nBOX_MATERIAL_FLUSH TNN=" + tnn + "\nBOX_NOZZLE_CLEAN");
    }
    // Use CR_BOX_* commands directly — M8200 macro's Jinja2 `params.I|int` is broken
    // on Creality's Klipper fork (always evaluates to 0, loading T1A regardless of I= value).
    // CR_BOX_PRE_OPT is required before extrude — sets CFS to material-change mode.
    // CR_BOX_WASTE must follow CR_BOX_EXTRUDE (purges transition material).
    // wipe_after=true: nozzle is full of fresh filament; wipe before parking.
    return wrap_with_park(variant,
                          "CR_BOX_PRE_OPT\nCR_BOX_EXTRUDE TNN=" + tnn +
                              "\nCR_BOX_WASTE\nCR_BOX_FLUSH TNN=" + tnn + "\nCR_BOX_END_OPT",
                          /*wipe_after=*/true);
}

std::string AmsBackendCfs::unload_gcode(CfsMacroVariant variant) {
    if (variant == CfsMacroVariant::Fork) {
        // Bare BOX_UNLOAD. It takes an optional MANUAL=0|1 (a user-driven
        // unload that skips the automated path) and explicitly REJECTS SLOT —
        // cmd_unload raises "BOX_UNLOAD no longer accepts SLOT". We always want
        // the automated path, so no parameters at all.
        return "BOX_UNLOAD";
    }
    if (variant == CfsMacroVariant::K1) {
        // K1: mirror the firmware BOX_QUIT_MATERIAL step list:
        //   ERROR_CLEAR → CHECK_MATERIAL → CUT → RETRUDE → safe park.
        // BOX_CUT_MATERIAL handles the cut; BOX_RETRUDE_MATERIAL is the no-TNN
        // retract primitive (operates on the currently-loaded slot tracked by
        // the box driver) — same one BOX_QUIT_MATERIAL uses in box.cfg. Nozzle
        // is empty after the cut+retract, so no wipe. Homing handled upstream.
        return wrap_with_envelope_k1("BOX_CUT_MATERIAL\n"
                                     "BOX_RETRUDE_MATERIAL");
    }
    // Unload ends with CR_BOX_RETRUDE — the nozzle is empty (cut + retracted),
    // so no wipe needed. Skipping the wipe also avoids the wipe macro pushing
    // anything back into the hotend during a "filament-out" state.
    // BOX_MODE_WAIT before CR_BOX_RETRUDE mirrors the stock
    // BOX_QUIT_MATERIAL_RETRUDE_MATERIAL sequence (state-machine settle
    // after the cut).
    return wrap_with_park(variant,
                          "CR_BOX_PRE_OPT\nCR_BOX_CUT\nBOX_MODE_WAIT\n"
                          "CR_BOX_RETRUDE\nCR_BOX_END_OPT",
                          /*wipe_after=*/false);
}

std::string AmsBackendCfs::swap_gcode(int idx, CfsMacroVariant variant) {
    if (variant == CfsMacroVariant::Fork) {
        // T<n> — box.py registers one per physical slot plus the external bay
        // (_register_t_commands) and routes it through its change engine, which
        // cuts, retracts, loads and flushes as one operation. There is no
        // BOX_CHANGE; that name appears only in a user-written alias macro that
        // this firmware does not define.
        //
        constexpr int CFS_MAX_SLOTS = 16;
        if (idx < 0 || idx >= CFS_MAX_SLOTS) {
            spdlog::error("[AMS CFS] Invalid slot index for swap: {}", idx);
            return "";
        }
        return "T" + std::to_string(idx);
    }
    std::string tnn = CfsMaterialDb::slot_to_tnn(idx);
    if (tnn.empty()) {
        spdlog::error("[AMS CFS] Invalid slot index for swap: {}", idx);
        return "";
    }
    if (variant == CfsMacroVariant::K1) {
        // K1: nozzle loaded — mirror the firmware BOX_LOAD_MATERIAL_WITH_MATERIAL
        // step list with explicit TNN=:
        //   ERROR_CLEAR → CHECK_MATERIAL → CUT → RETRUDE → GO_TO_EXTRUDE_POS →
        //   NOZZLE_CLEAN → EXTRUDE → EXTRUDER_EXTRUDE → FLUSH → safe park.
        // No BOX_MODE_WAIT on K1 firmware. BOX_EXTRUDER_EXTRUDE between EXTRUDE
        // and FLUSH is the same auto-extrude fix as the fresh-load path (#968).
        // Wipe (NOZZLE_CLEAN) precedes the new feed here exactly as the firmware
        // WITH_MATERIAL chain does. Homing handled upstream.
        return wrap_with_envelope_k1("BOX_CUT_MATERIAL\n"
                                     "BOX_RETRUDE_MATERIAL\n"
                                     "BOX_GO_TO_EXTRUDE_POS\n"
                                     "BOX_NOZZLE_CLEAN\n"
                                     "BOX_EXTRUDE_MATERIAL TNN=" +
                                     tnn + "\nBOX_EXTRUDER_EXTRUDE TNN=" + tnn +
                                     "\nBOX_MATERIAL_FLUSH TNN=" + tnn);
    }
    // Full swap: unload current (cut+retract) then load new slot, all in one
    // session. BOX_MODE_WAIT interposed after CR_BOX_CUT (let the cutter
    // recover) and before CR_BOX_EXTRUDE (new slot's state-machine handoff).
    // Ends with flush of the NEW filament so wipe is required, same as load.
    return wrap_with_park(variant,
                          "CR_BOX_PRE_OPT\nCR_BOX_CUT\nBOX_MODE_WAIT\n"
                          "CR_BOX_RETRUDE\nBOX_MODE_WAIT\n"
                          "CR_BOX_EXTRUDE TNN=" +
                              tnn + "\nCR_BOX_WASTE\nCR_BOX_FLUSH TNN=" + tnn + "\nCR_BOX_END_OPT",
                          /*wipe_after=*/true);
}

AmsError AmsBackendCfs::dispatch_action_script(std::string gcode) {
    if (!api_) {
        return AmsErrorHelper::not_connected("IMoonrakerAPI not available");
    }

    auto on_complete = [this]() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (system_info_.action != AmsAction::IDLE) {
            spdlog::info("[AMS CFS] Action script complete — action {} -> IDLE",
                         static_cast<int>(system_info_.action));
            system_info_.action = AmsAction::IDLE;
            end_phase_tracking();
            PostOpCooldownManager::instance().schedule();
        }
    };

    auto token = lifetime_.token();
    auto on_error = [this, token](const MoonrakerError& err) {
        // L081 Mechanism C: marshal member writes (system_info_) to main.
        token.defer("AmsBackendCfs::action_err", [this, err]() {
            // Klipper rejection (key849, busy, etc.) and timeouts both land here.
            // Either way the driver isn't running our script anymore, so flip back
            // to IDLE so the UI doesn't get stuck on a "loading" spinner.
            spdlog::error("[AMS CFS] Action script failed: {}", err.message);
            {
                std::lock_guard<std::mutex> lock(mutex_);
                system_info_.action = AmsAction::IDLE;
                end_phase_tracking();
            }

            // Best-effort unwind. The wrap_with_park envelope emits
            // BOX_SAVE_FAN + SAVE_GCODE_STATE upfront and relies on the
            // tail of the script to restore them — but Klipper macros
            // have no try/finally, so if the body raised mid-sequence
            // (key849, key851, min_extrude_temp, etc.) the restore lines
            // never ran. Fire each cleanup as a separate gcode_script
            // with empty callbacks so one failing (e.g. RESTORE_GCODE_STATE
            // when no save exists because SAVE never ran) doesn't block
            // the other. Worst case: cleanup is a no-op.
            if (api_) {
                // BOX_RESTORE_FAN only exists on K2 firmware (and only the K2
                // envelope ran BOX_SAVE_FAN). The K1 official CFS upgrade
                // firmware lacks the macro and never saved fan state, so emitting
                // it there just returns key61 — skip it on K1. (#968)
                if (macro_variant_ == CfsMacroVariant::K2) {
                    api_->execute_gcode(
                        "BOX_RESTORE_FAN", []() {}, [](const MoonrakerError&) {},
                        IMoonrakerAPI::AMS_OPERATION_TIMEOUT_MS);
                }
                if (macro_variant_ != CfsMacroVariant::Fork) {
                    api_->execute_gcode(
                        "RESTORE_GCODE_STATE NAME=helix_cfs_load", []() {},
                        [](const MoonrakerError&) {}, IMoonrakerAPI::AMS_OPERATION_TIMEOUT_MS);
                }
            }
        });
    };

    // Homing, timeout, error plumbing and the Fork bypass all live in the base
    // helper now. What stays here is CFS's own unwind: phase tracking, the K2
    // fan restore, and RESTORE_GCODE_STATE.
    return ensure_homed_then(std::move(gcode), std::move(on_complete), std::move(on_error),
                             IMoonrakerAPI::AMS_OPERATION_TIMEOUT_MS,
                             /*skip_homing=*/macro_variant_ == CfsMacroVariant::Fork,
                             /*silent=*/false);
}

// ============================================================================
// Sub-phase synthesis (Task #2)
//
// CFS macros decompose into Heat → Cut → Retract → Feed → Purge but the
// backend sets system_info_.action once at dispatch and leaves it until the
// gcode-script completion callback. We synthesize per-phase transitions from
// physical signals so the UI's step indicator advances correctly:
//
//   - filament_switch_sensor edge true→false  ⇒ cut just completed
//   - filament_switch_sensor edge false→true  ⇒ new filament reached extruder
//   - extruder.target rising >10°C above the post-heat baseline ⇒ purge phase
//
// Heat anchoring lives in the UI (AmsOperationSidebar) and overrides any
// action we publish here while current_temp < target - 5°C.
// ============================================================================

void AmsBackendCfs::begin_phase_tracking() {
    phase_tracker_ = PhaseTracker{};
    phase_tracker_.active = true;
    phase_tracker_.started_with_filament = system_info_.filament_loaded;
    // Don't latch baseline_target until heating completes — the macro raises
    // target as its first act, and that rise is not a purge signal.
}

void AmsBackendCfs::end_phase_tracking() {
    phase_tracker_ = PhaseTracker{};
}

void AmsBackendCfs::on_filament_transition_locked(bool new_detected) {
    if (!phase_tracker_.active) {
        spdlog::debug("[AMS CFS] Phase: filament {} -> {} (no active op)", last_filament_detected_,
                      new_detected);
        return;
    }
    if (last_filament_detected_ && !new_detected) {
        phase_tracker_.seen_filament_drop = true;
        spdlog::info("[AMS CFS] Phase: filament drop (cut completed)");
    } else if (!last_filament_detected_ && new_detected && phase_tracker_.seen_filament_drop) {
        phase_tracker_.seen_filament_rise = true;
        spdlog::info("[AMS CFS] Phase: filament rise after drop (swap feed complete)");
    } else if (!last_filament_detected_ && new_detected && !phase_tracker_.started_with_filament) {
        // Fresh-load case: filament arrives without a prior drop. Treat the
        // rise as the start of the post-feed phase.
        phase_tracker_.seen_filament_rise = true;
        spdlog::info("[AMS CFS] Phase: filament rise (fresh feed complete)");
    }
    // Promote a pending purge target (latched earlier when the target rose
    // before feed completed) once the rise actually happens.
    if (phase_tracker_.seen_filament_rise && phase_tracker_.pending_purge_target &&
        !phase_tracker_.seen_purge_signal) {
        phase_tracker_.seen_purge_signal = true;
        spdlog::info("[AMS CFS] Phase: purge promoted (pending target jump + feed complete)");
    }
    apply_synthesized_action_locked();
}

void AmsBackendCfs::on_extruder_temp_change_locked(int new_temp_deci, int new_target_deci) {
    if (!phase_tracker_.active)
        return;

    if (new_target_deci > 0 && new_temp_deci >= (new_target_deci - 50 /* 5°C deci */)) {
        if (!phase_tracker_.reached_target_once) {
            phase_tracker_.reached_target_once = true;
            phase_tracker_.baseline_target_deci = new_target_deci;
            spdlog::debug("[AMS CFS] Phase: reached target {}°C, baseline latched",
                          helix::ui::temperature::deci_to_degrees(new_target_deci));
        }
    }

    // Purge detection: target rises >10°C above the post-heat baseline. The
    // K2 macro raises target multiple times — a 220→235 "operational" jump
    // very early (before cut), then 235→240 (flush_max_temp) at purge.
    // Macro variants may also do a single 220→240 jump well before feed.
    // Latch the target-jump observation in pending_purge_target regardless
    // of filament state, then promote to seen_purge_signal once the feed
    // physically completes (seen_filament_rise = true). UNLOAD has no rise,
    // so promotion never happens and purge correctly stays off.
    if (phase_tracker_.reached_target_once && phase_tracker_.baseline_target_deci > 0 &&
        new_target_deci > phase_tracker_.baseline_target_deci + 100 /* 10°C deci */ &&
        !phase_tracker_.pending_purge_target) {
        phase_tracker_.pending_purge_target = true;
        spdlog::debug(
            "[AMS CFS] Phase: purge target jump latched (target {}°C > baseline {}°C + 10)",
            helix::ui::temperature::deci_to_degrees(new_target_deci),
            helix::ui::temperature::deci_to_degrees(phase_tracker_.baseline_target_deci));
    }
    if (phase_tracker_.pending_purge_target && phase_tracker_.seen_filament_rise &&
        !phase_tracker_.seen_purge_signal) {
        phase_tracker_.seen_purge_signal = true;
        spdlog::info("[AMS CFS] Phase: purge promoted (target jump + feed complete)");
    }

    apply_synthesized_action_locked();
}

void AmsBackendCfs::apply_synthesized_action_locked() {
    if (!phase_tracker_.active)
        return;

    AmsAction synth;
    if (phase_tracker_.seen_purge_signal) {
        synth = AmsAction::PURGING;
    } else if (phase_tracker_.started_with_filament && !phase_tracker_.seen_filament_drop) {
        // Swap or unload, before the cutter trips — Cut/Tip phase.
        synth = AmsAction::CUTTING;
    } else if (phase_tracker_.seen_filament_drop && !phase_tracker_.seen_filament_rise) {
        // After cut, before new filament arrives (or whole unload retract).
        synth = AmsAction::UNLOADING;
    } else {
        // Fresh-load feed, post-feed swap, or post-purge tail.
        synth = AmsAction::LOADING;
    }

    if (system_info_.action != synth && system_info_.action != AmsAction::IDLE) {
        spdlog::info("[AMS CFS] Phase synth: {} -> {}", ams_action_to_string(system_info_.action),
                     ams_action_to_string(synth));
        system_info_.action = synth;
    }
}

std::string AmsBackendCfs::reset_gcode() {
    return "BOX_ERROR_CLEAR";
}

std::string AmsBackendCfs::recover_gcode() {
    return "BOX_ERROR_RESUME_PROCESS";
}

// --- Error-center bridge ---

std::vector<helix::RecoveryAction> AmsBackendCfs::build_recovery_actions() const {
    // Caller holds mutex_.
    std::vector<helix::RecoveryAction> actions;

    // Primary: plain RESUME, deliberately NOT recover_gcode()'s
    // BOX_ERROR_RESUME_PROCESS. That command only drives the box half of the
    // recovery and leaves the job paused; the firmware reaches it FROM `RESUME`
    // via RESUME_EXTERNAL_PROCESS (gcode_macro.cfg), and it early-returns unless
    // the box is enabled AND print_stats.state is already 'paused'. RESUME is
    // therefore the only button that both restarts the job and lets the box do
    // its part. Resuming extrudes on the next move, so the hotend has to be up:
    // the give-up path stops the print and idle_timeout or the post-op cooldown
    // has usually taken the heater down by the time anyone taps this.
    actions.push_back({lv_tr("Resume"), "RESUME", "cfs::resume", "primary",
                       /*needs_hot_nozzle=*/true});

    // Last resort. RESUME silently does nothing when the box latched an error or
    // was disabled (the `enable != 0` half of BOX_ERROR_RESUME_PROCESS's guard),
    // and this is the lever that clears that. Same label and gcode the generic
    // classifier offers for key840 so the button reads the same wherever it
    // appears; reset_gcode() rather than a second "BOX_ERROR_CLEAR" literal.
    // Clears state only — no filament moves, so it stays tappable cold.
    actions.push_back({lv_tr("Reset CFS"), reset_gcode(), "cfs::error_clear", "danger"});
    return actions;
}

std::optional<helix::ErrorEvent>
AmsBackendCfs::classify_error(const std::string& raw_line,
                              const helix::ClassifyContext& ctx) const {
    // `!!` lines are NOT ours. Every CFS fault Klipper broadcasts carries a
    // key8xx code that error_classify::classify() already decodes (CRITICAL, and
    // a "Reset CFS" action for key840). Claiming them here would either
    // duplicate that or silently replace it. The give-up messages below arrive
    // on the plain response channel instead, which no classifier looks at today.
    if (helix::is_bang_line(raw_line)) {
        return std::nullopt;
    }

    // The box only ever reaches these messages from its runout handler, and that
    // handler runs behind a pause: [filament_switch_sensor filament_sensor] has
    // pause_on_runout = true, so Klipper issues PAUSE first and only then runs
    // runout_gcode -> BOX_CHECK_MATERIAL_REFILL (verified in the live K2 Plus
    // config dump). Requiring PAUSED is what keeps a human echoing the same words
    // into the console, or poking the macro by hand on an idle machine, from
    // popping a runout modal.
    if (!ctx.is_paused) {
        return std::nullopt;
    }

    // Klipper's respond_info() emits "// <text>". Strip it for display; match on
    // the remainder so a firmware that drops the prefix still hits.
    std::string detail = raw_line;
    if (detail.rfind("// ", 0) == 0) {
        detail = detail.substr(3);
    } else if (detail.rfind("//", 0) == 0) {
        detail = detail.substr(2);
    }

    // The two give-up paths, matched on the distinctive fragment rather than the
    // whole sentence. These are untranslated English literals emitted by one
    // Creality firmware build, so the exact wording is the least durable part of
    // this: matching a fragment survives the surrounding sentence being reworded
    // ("no identical supplies" / "there are no identical supplies", "disable
    // material automatic refill" / "material automatic refill is disabled").
    const bool no_matching_spool = helix::contains_ci(detail, "identical suppl");
    const bool refill_disabled =
        helix::contains_ci(detail, "automatic refill") && helix::contains_ci(detail, "disab");

    std::lock_guard<std::mutex> lock(mutex_);

    // Fallback tier, for the day the wording changes out from under both matches
    // above. It asks for far less of the string — only that the line is about
    // refilling — and makes up the difference with machine state: the box itself
    // says there is no filament at the gate (filament_useup / the flat schema's
    // `runout`) and the job is paused. Weaker evidence, so it surfaces the
    // firmware's own words rather than a claim about which give-up path ran.
    const bool weak_refill_hint = !no_matching_spool && !refill_disabled &&
                                  helix::contains_ci(detail, "refill") &&
                                  system_info_.filament_runout;

    if (!no_matching_spool && !refill_disabled && !weak_refill_hint) {
        return std::nullopt;
    }

    std::string message;
    if (no_matching_spool) {
        message = lv_tr("Filament ran out and the CFS found no matching spool to switch to. "
                        "Load a spool of the same material and color, then resume.");
    } else if (refill_disabled) {
        message = lv_tr("Filament ran out. Auto-refill is off, so the CFS will not switch "
                        "spools on its own. Load filament, then resume.");
    } else {
        message = detail;
    }

    spdlog::warn("{} Runout: CFS declined to auto-refill ({}): {}", backend_log_tag(),
                 no_matching_spool ? "no matching spool"
                                   : (refill_disabled ? "auto-refill off" : "unrecognized wording"),
                 detail);

    helix::ErrorEvent e = helix::make_ams_fault_event(
        helix::ErrorSource::CFS, lv_tr("Filament runout"), message, build_recovery_actions());
    // The firmware's untranslated wording is the cross-channel dedup identity —
    // make_ams_fault_event leaves raw_detail empty, and without it the router
    // would record only the translated sentence, which nothing else can match.
    e.raw_detail = detail;
    return e;
}

// --- Capabilities ---

helix::printer::EndlessSpoolCapabilities AmsBackendCfs::get_endless_spool_capabilities() const {
    std::lock_guard<std::mutex> lock(mutex_);
    using namespace helix::printer;
    return {.availability = EndlessSpoolAvailability::Available,
            .enabled = system_info_.endless_spool_enabled ? EndlessSpoolEnabled::On
                                                          : EndlessSpoolEnabled::Off,
            .editability = EndlessSpoolEditability::ReadOnly,
            .restriction = EndlessSpoolRestriction::FirmwareManaged};
}

helix::printer::ToolMappingCapabilities AmsBackendCfs::get_tool_mapping_capabilities() const {
    return {.supported = true,
            .editable = true,
            .description = "Tool reassignment via BOX_MODIFY_TN"}; // i18n: do not translate
}

std::vector<int> AmsBackendCfs::get_tool_mapping() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return system_info_.tool_to_slot_map;
}

bool AmsBackendCfs::reports_firmware_tool_mapping() const {
    // Everywhere except the K1 official CFS upgrade firmware, where
    // BOX_MODIFY_TN is a confirmed no-op (#968 Phase 5): the command is
    // accepted and nothing changes, so no box frame with a new map ever
    // arrives. Claiming confirmation support there would leave a restore
    // waiting forever and strand pending_remap.json (#1270).
    return macro_variant_ != CfsMacroVariant::K1;
}

uint64_t AmsBackendCfs::firmware_tool_mapping_generation() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return firmware_map_generation_;
}

std::vector<helix::printer::DeviceAction> AmsBackendCfs::get_device_actions() const {
    using DA = helix::printer::DeviceAction;
    using AT = helix::printer::ActionType;
    return {
        DA{"refresh_rfid",
           "Refresh RFID",
           "",
           "",
           "Re-read spool RFID tags and remaining length",
           AT::BUTTON,
           {},
           {},
           0,
           100,
           "",
           -1,
           true,
           ""},
        DA{"toggle_auto_refill",
           "Toggle Auto-Refill",
           "",
           "",
           "Enable/disable automatic backup spool switching",
           AT::TOGGLE,
           {},
           {},
           0,
           100,
           "",
           -1,
           true,
           ""},
        DA{"nozzle_clean",
           "Clean Nozzle",
           "",
           "",
           "Wipe nozzle on silicone cleaning strip",
           AT::BUTTON,
           {},
           {},
           0,
           100,
           "",
           -1,
           true,
           ""},
        DA{"comm_test",
           "Communication Test",
           "",
           "",
           "Test RS-485 link to CFS units",
           AT::BUTTON,
           {},
           {},
           0,
           100,
           "",
           -1,
           true,
           ""},
    };
}

AmsError AmsBackendCfs::execute_device_action(const std::string& action_id,
                                              const std::any& /*value*/) {
    if (action_id == "refresh_rfid") {
        // Probe every connected CFS unit's RFID tags. Inserting a spool does NOT
        // auto-read its tag: the box reports vender/color/material as sentinels
        // until BOX_INFO_REFRESH scans them (spool then shows as "unknown"/empty
        // in the UI until refreshed). ADDR is the 1-based unit index; NUM is a
        // per-slot bitflag (A=1, B=2, C=4, D=8), so NUM=15 (0b1111) refreshes all
        // four slots of the unit. Sent verbatim on both K2 and K1 macro variants,
        // like the other BOX_* control commands. (prestonbrown/helixscreen#1077,
        // workflow reported by cubewhy.)
        std::vector<int> unit_addrs;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (const auto& unit : system_info_.units) {
                if (unit.connected) {
                    unit_addrs.push_back(unit.unit_index + 1);
                }
            }
        }
        for (int addr : unit_addrs) {
            execute_gcode("BOX_INFO_REFRESH ADDR=" + std::to_string(addr) + " NUM=15");
        }
        // Re-query box state so the freshly-probed RFID/length values land in the
        // UI. The box module publishes the updated `box` object after the scan.
        on_started();
        return AmsErrorHelper::success();
    }

    if (action_id == "toggle_auto_refill") {
        return execute_gcode("BOX_ENABLE_AUTO_REFILL");
    }

    if (action_id == "nozzle_clean") {
        return execute_gcode("BOX_NOZZLE_CLEAN");
    }

    if (action_id == "comm_test") {
        // Query box state as a connectivity test
        on_started();
        return AmsErrorHelper::success();
    }

    return AmsErrorHelper::not_supported("Unknown action: " + action_id);
}

// ============================================================================
// Override layering (shared FilamentSlotOverrideStore)
// ============================================================================

void AmsBackendCfs::apply_overrides(SlotInfo& slot, int slot_index) {
    // Every caller of apply_overrides runs under mutex_ (handle_status_update
    // post-parse loop). overrides_ writers also hold mutex_, so the map read
    // here is implicitly lock-protected. Zero-cost hash miss when the slot
    // has no override — safe in the hot parse path.
    auto it = overrides_.find(slot_index);
    if (it == overrides_.end())
        return;
    const auto& o = it->second;
    // Merge policy matches Snapmaker / ACE. Override wins only when the
    // override field carries a real value; defaults fall through to firmware.
    if (!o.brand.empty())
        slot.brand = o.brand;
    if (!o.spool_name.empty())
        slot.spool_name = o.spool_name;
    if (o.spoolman_id > 0)
        slot.spoolman_id = o.spoolman_id;
    if (o.spoolman_vendor_id > 0)
        slot.spoolman_vendor_id = o.spoolman_vendor_id;
    if (o.remaining_weight_g >= 0.0f)
        slot.remaining_weight_g = o.remaining_weight_g;
    if (o.total_weight_g >= 0.0f)
        slot.total_weight_g = o.total_weight_g;
    if (o.color_set)
        slot.color_rgb = o.color_rgb;
    if (!o.color_name.empty())
        slot.color_name = o.color_name;
    if (!o.material.empty())
        slot.material = o.material;
    // Catalog product identity — same "override wins only when it carries a
    // real value" rule as the strings above. Firmware never populates these
    // (no AMS protocol has a notion of a branded product id), so a non-empty
    // value here is always a user pick and always wins.
    if (!o.catalog_id.empty())
        slot.catalog_id = o.catalog_id;
    if (!o.product_name.empty())
        slot.product_name = o.product_name;

    // Trust the user's assignment for presence. Untagged 3rd-party spools
    // always read RFID -1, so firmware reports the bay EMPTY even though a
    // spool is physically present. If the override carries a real assignment,
    // the user has told us a spool is in this bay — promote it to AVAILABLE.
    const bool real_assignment = o.spoolman_id > 0 || !o.material.empty() || !o.brand.empty() ||
                                 !o.spool_name.empty() || o.color_set;
    if (real_assignment && slot.status == SlotStatus::EMPTY) {
        slot.status = SlotStatus::AVAILABLE;
    }
}

bool AmsBackendCfs::check_hardware_event_clear(SlotInfo& slot, int slot_index,
                                               const std::string& observed_uid) {
    std::string old_uid;
    const auto event = rfid_tracker_.observe(slot_index, observed_uid, &old_uid);

    switch (event) {
    case helix::ams::FingerprintEvent::NoSignal:
        // No RFID tag / sentinel material_type / sentinel color_value, or the
        // slot wasn't in this incremental update. Baseline untouched, no clear.
        return false;

    case helix::ams::FingerprintEvent::Baseline:
        spdlog::debug("{} Slot {} baseline RFID fingerprint: {}", backend_log_tag(), slot_index,
                      observed_uid);
        return false;

    case helix::ams::FingerprintEvent::Unchanged:
        return false;

    case helix::ams::FingerprintEvent::OwnWriteEcho:
        // Firmware just handed back the color_value we wrote in
        // push_slot_color_to_firmware. The physical spool never moved, so the
        // user's override stands. The baseline has already advanced to the
        // echoed value, so the NEXT genuine swap is detected against it.
        spdlog::debug("{} Slot {} RFID fingerprint {} -> {} matches our own color push — "
                      "override retained",
                      backend_log_tag(), slot_index, old_uid, observed_uid);
        return false;

    case helix::ams::FingerprintEvent::Changed:
        break;
    }

    auto ovr_it = overrides_.find(slot_index);
    if (ovr_it == overrides_.end()) {
        spdlog::debug("{} Slot {} RFID fingerprint changed {} -> {} (no override to clear)",
                      backend_log_tag(), slot_index, old_uid, observed_uid);
        return false;
    }

    spdlog::info("{} Slot {} RFID fingerprint changed {} -> {}, clearing override "
                 "(physical spool swap detected)",
                 backend_log_tag(), slot_index, old_uid, observed_uid);

    // Delegate erase + field reset + clear_async to the shared helper so
    // hardware-event clears and user-initiated clears share one field-reset
    // policy. Caller already holds mutex_.
    (void)ovr_it;
    clear_override_locked(slot_index, slot);
    return true;
}

bool AmsBackendCfs::clear_stale_override_on_removal_locked(SlotInfo& slot, int slot_index) {
    if (slot.status != SlotStatus::EMPTY)
        return false;

    auto it = overrides_.find(slot_index);
    if (it == overrides_.end())
        return false;

    // A deliberate user assignment survives an empty bay. The user told us what
    // belongs in this slot; unloading it — or a reader that momentarily can't
    // see the tag — must not throw that away. Locks are the authoritative
    // "the user chose this" signal and they round-trip through lane_data, so
    // this holds across restarts too. Legacy records load with locks defaulted
    // to true wherever the field has a value, so pre-lock user data is covered.
    const auto& o = it->second;
    if (o.user_locked_color || o.user_locked_material) {
        return false;
    }

    // Identity fields are equally authoritative, and locks alone don't cover
    // them. The FillUnsetOnly auto-mirror writes ONLY color_rgb/color_set and
    // material (see mirror_firmware_to_lane_data) — it can never populate a
    // brand, spool name or Spoolman id. So an override carrying any of those
    // came from a user assignment (or a Spoolman link), which must survive an
    // empty bay exactly like a locked field does. Without this, a bay that
    // reads EMPTY for one poll — a genuine unload, but equally a transient
    // unreadable-tag read — silently destroys the user's Spoolman linkage.
    //
    // catalog_id belongs in the same list and is NOT redundant with brand: a
    // Generic catalog product carries an empty brand, so a user who picked one
    // leaves brand/spool_name/ids all empty and would fall through to the
    // auto-mirror-residue verdict below. Nothing but a user pick can ever write
    // it — the mirror has no notion of a catalog product.
    if (!o.brand.empty() || !o.spool_name.empty() || o.spoolman_id != 0 ||
        o.spoolman_vendor_id != 0 || !o.catalog_id.empty() || !o.product_name.empty()) {
        return false;
    }

    // What's left in overrides_ for an empty bay is pure firmware auto-mirror
    // residue — color/material describing a spool that is no longer seated.
    // Erase it so the lane_data record stops advertising a stale color/material
    // to OrcaSlicer and apply_overrides stops promoting the bay back to
    // AVAILABLE.
    spdlog::info("{} Slot {} reads EMPTY — clearing auto-mirrored override for the removed spool",
                 backend_log_tag(), slot_index);
    clear_override_locked(slot_index, slot);
    return true;
}

void AmsBackendCfs::clear_override_locked(int slot_index, SlotInfo& slot) {
    // Caller must hold mutex_. Erases the in-memory override, resets STRICTLY
    // override-exclusive fields on the live SlotInfo so the cleared state is
    // visible in the very next get_slot_info() read (apply_overrides is a
    // no-op for this slot afterwards).
    //
    // CFS field policy: brand / color_name / total_weight_g come from the
    // RFID material database (FilamentCatalog::resolve_code lookup in
    // parse_box_status) — the parse has already written firmware truth for
    // the current spool, so
    // we must NOT re-zero those fields. The override's copies disappear with
    // the erase; firmware's copies stay. Matches Snapmaker policy.
    overrides_.erase(slot_index);

    slot.spool_name.clear();
    slot.spoolman_id = 0;
    slot.spoolman_vendor_id = 0;
    slot.remaining_weight_g = -1.0f;
    // The catalog pick is override-exclusive on every backend — no AMS
    // firmware carries a branded product id — so a clear always drops it.
    // Leaving it would re-navigate the editor to the removed spool's
    // product on the next open.
    slot.catalog_id.clear();
    slot.product_name.clear();

    if (override_store_) {
        // Capture by value — clear_async's Moonraker callback may fire after
        // this returns (MR tracker ~60s) and potentially after the backend
        // itself is gone. Same rationale as save_async.
        const std::string tag = backend_log_tag();
        override_store_->clear_async(slot_index, [tag, slot_index](bool ok, std::string err) {
            if (!ok) {
                spdlog::warn("{} clear_async failed for slot {}: {}", tag, slot_index, err);
            }
        });
    }
}

void AmsBackendCfs::clear_slot_override(int slot_index) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto* slot = system_info_.get_slot_global(slot_index);
        if (!slot) {
            spdlog::warn("{} clear_slot_override: no slot entry for global index {}",
                         backend_log_tag(), slot_index);
            return;
        }
        spdlog::info("{} Slot {} override cleared by user request", backend_log_tag(), slot_index);
        clear_override_locked(slot_index, *slot);
    }

    emit_event(EVENT_SLOT_CHANGED, std::to_string(slot_index));
}

void AmsBackendCfs::clear_box_slot_profile(int slot_index) {
    if (macro_variant_ == CfsMacroVariant::Fork) {
        execute_gcode("_BOX_SLOT_CLEAR SLOT=" + std::to_string(slot_index));
    }
}

} // namespace helix::printer

#endif // HELIX_HAS_CFS
