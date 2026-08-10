// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ams_backend_multiace.h"

#include "json_utils.h"

#include <spdlog/fmt/fmt.h>
#include <spdlog/spdlog.h>

#include <algorithm>

namespace {

/// multiACE keys its per-head maps by the head index rendered as a STRING
/// ("0".."3"), not as a JSON int. Reading them with an int key silently finds
/// nothing and every head looks feeder-fed.
template <typename T>
std::optional<T> head_keyed(const nlohmann::json& obj, int head) {
    if (!obj.is_object()) {
        return std::nullopt;
    }
    auto it = obj.find(std::to_string(head));
    if (it == obj.end() || it->is_null()) {
        return std::nullopt;
    }
    try {
        return it->get<T>();
    } catch (...) {
        return std::nullopt;
    }
}

/// `slots[].color` is an [r,g,b] array of ints, not a hex string. Returns
/// nullopt for a missing/short array so a slot with no colour keeps the
/// SlotInfo default rather than being forced to black.
std::optional<uint32_t> color_array_to_rgb(const nlohmann::json& c) {
    if (!c.is_array() || c.size() < 3) {
        return std::nullopt;
    }
    auto ch = [&](size_t i) -> uint32_t {
        return c[i].is_number_integer()
                   ? static_cast<uint32_t>(std::clamp(c[i].get<int>(), 0, 255))
                   : 0u;
    };
    return (ch(0) << 16) | (ch(1) << 8) | ch(2);
}

/// Model name for an ACE unit, from the only field that distinguishes them.
/// multiACE's own FILAMENT_MOTION_FEATURE_REFERENCE.md § 10 names the two
/// protocol generations "ACE Pro v1" and "ACE 2 Pro v2" — note the digit sits
/// in the middle, so it is "ACE 2 Pro", not "ACE Pro 2". An unrecognised or
/// absent protocol falls back to the bare family name rather than guessing.
const char* ace_model_name(const std::string& protocol) {
    if (protocol == "v2") {
        return "ACE 2 Pro"; // i18n: do not translate - product name
    }
    if (protocol == "v1") {
        return "ACE Pro"; // i18n: do not translate - product name
    }
    return "ACE"; // i18n: do not translate - product name
}

} // namespace

AmsBackendMultiAce::AmsBackendMultiAce(MoonrakerAPI* api, helix::MoonrakerClient* client)
    : AmsBackendSnapmaker(api, client) {
    // The base ctor has already built unit 0 (the U1's four heads) and every
    // Snapmaker capability flag. Only the identity changes here; ACE units are
    // added when the first `ace` frame arrives, because their count is not
    // known until then.
    system_info_.type_name = "Snapmaker U1 + multiACE";
    spdlog::debug("[AmsBackendMultiAce] Constructed (unit 0 = U1 SnapSwap, ACE units pending)");
}

// ============================================================================
// Queries
// ============================================================================

PathTopology AmsBackendMultiAce::get_unit_topology(int unit_index) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (unit_index < 0 || unit_index >= static_cast<int>(system_info_.units.size())) {
        return PathTopology::PARALLEL; // the U1 itself, and the safe default
    }
    return system_info_.units[unit_index].topology;
}

std::optional<int> AmsBackendMultiAce::slot_identity_owner_unit(int slot_index) const {
    std::lock_guard<std::mutex> lock(mutex_);
    // Only the U1's own heads (unit 0) can be fed from elsewhere. An ACE bay is
    // the source, so it always describes itself.
    if (slot_index < 0 || slot_index >= NUM_TOOLS) {
        return std::nullopt;
    }
    if (head_kind_[slot_index] != HeadSource::ACE || !head_seated_[slot_index]) {
        return std::nullopt;
    }
    return head_seated_[slot_index]->unit_index;
}

AmsBackendMultiAce::HeadSource AmsBackendMultiAce::head_source_kind(int head) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (head < 0 || head >= NUM_TOOLS) {
        return HeadSource::UNKNOWN;
    }
    return head_kind_[head];
}

std::optional<AmsBackendMultiAce::SeatedSource>
AmsBackendMultiAce::seated_source(int head) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (head < 0 || head >= NUM_TOOLS) {
        return std::nullopt;
    }
    return head_seated_[head];
}

// ============================================================================
// Status parsing
// ============================================================================

void AmsBackendMultiAce::handle_status_update(const nlohmann::json& notification) {
    // The U1 half is unchanged and must run first — it owns unit 0, the load
    // latch, the sensors and the tool election.
    AmsBackendSnapmaker::handle_status_update(notification);

    // Same unwrapping the base does, and it must match exactly:
    // notify_status_update arrives as {"method":..., "params":[{...}, ts]},
    // while the initial query response is the bare status object. Reaching for
    // a "status" key instead finds neither, so the backend starts, subscribes,
    // and then silently behaves like the plain Snapmaker one.
    const nlohmann::json* status_ptr = &notification;
    if (notification.contains("params") && notification["params"].is_array() &&
        !notification["params"].empty()) {
        status_ptr = &notification["params"][0];
    }
    const auto& status = *status_ptr;
    if (!status.is_object() || !status.contains("ace") || !status["ace"].is_object()) {
        return;
    }

    bool changed = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        parse_ace_object_locked(status["ace"], changed);
    }
    if (changed) {
        emit_event(EVENT_STATE_CHANGED);
    }
}

void AmsBackendMultiAce::parse_ace_object_locked(const nlohmann::json& ace, bool& changed) {
    if (ace.contains("mode") && ace["mode"].is_string()) {
        const bool head_mode = ace["mode"].get<std::string>() == "head";
        if (head_mode != head_mode_) {
            head_mode_ = head_mode;
            changed = true;
        }
    }
    if (ace.contains("device_count") && ace["device_count"].is_number_integer()) {
        const int n = std::clamp(ace["device_count"].get<int>(), 0, MAX_ACE_UNITS);
        if (n != device_count_) {
            device_count_ = n;
            changed = true;
        }
    }

    // Per-head source kind. Order matters and is not arbitrary: `head_ace`
    // carries an ACE index for EVERY head (on the live U1 it reads
    // {0:0,1:1,2:2,3:0} while only head 3 is ACE-fed), so it cannot be used to
    // decide *whether* a head is ACE-fed. head_feeder/head_manual are the
    // authority; ACE is what is left over.
    const auto& manual = ace.contains("head_manual") ? ace["head_manual"] : nlohmann::json::object();
    const auto& feeder = ace.contains("head_feeder") ? ace["head_feeder"] : nlohmann::json::object();
    const bool have_maps = manual.is_object() || feeder.is_object();

    for (int h = 0; h < NUM_TOOLS; ++h) {
        HeadSource kind = HeadSource::UNKNOWN;
        if (have_maps) {
            if (head_keyed<bool>(manual, h).value_or(false)) {
                kind = HeadSource::MANUAL;
            } else if (head_keyed<bool>(feeder, h).value_or(false)) {
                kind = HeadSource::FEEDER;
            } else {
                kind = HeadSource::ACE;
            }
        }
        if (kind != head_kind_[h]) {
            head_kind_[h] = kind;
            changed = true;
        }

        std::optional<SeatedSource> seated;
        if (ace.contains("head_source") && ace["head_source"].is_object()) {
            auto it = ace["head_source"].find(std::to_string(h));
            if (it != ace["head_source"].end() && it->is_object()) {
                const int ai = helix::json_util::safe_int(*it, "ace_index", -1);
                const int sl = helix::json_util::safe_int(*it, "slot", -1);
                if (ai >= 0 && ai < MAX_ACE_UNITS && sl >= 0) {
                    seated = SeatedSource{/*unit_index=*/ai + 1, ai, sl};
                }
            }
        }
        if (seated.has_value() != head_seated_[h].has_value() ||
            (seated && head_seated_[h] &&
             (seated->ace_index != head_seated_[h]->ace_index ||
              seated->slot != head_seated_[h]->slot))) {
            head_seated_[h] = seated;
            changed = true;
        }
    }

    // Per-unit inventory.
    if (ace.contains("aces") && ace["aces"].is_array()) {
        for (const auto& unit : ace["aces"]) {
            if (!unit.is_object()) {
                continue;
            }
            const int idx = helix::json_util::safe_int(unit, "idx", -1);
            if (idx < 0 || idx >= MAX_ACE_UNITS) {
                continue;
            }
            auto& st = ace_units_[idx];
            st.connected = unit.value("connected", st.connected);
            st.protocol = helix::json_util::safe_string(unit, "protocol", st.protocol);
            st.temp = helix::json_util::safe_int(unit, "temp", st.temp);
            st.humidity = helix::json_util::safe_int(unit, "humidity", st.humidity);

            if (unit.contains("gate_status") && unit["gate_status"].is_array()) {
                const auto& gs = unit["gate_status"];
                for (int s = 0; s < ACE_SLOTS_PER_UNIT && s < static_cast<int>(gs.size()); ++s) {
                    st.gate_present[s] = gs[s].is_number_integer() && gs[s].get<int>() != 0;
                }
            }
            if (unit.contains("slots") && unit["slots"].is_array()) {
                const auto& slots = unit["slots"];
                for (int s = 0; s < ACE_SLOTS_PER_UNIT && s < static_cast<int>(slots.size()); ++s) {
                    if (!slots[s].is_object()) {
                        continue;
                    }
                    st.material[s] = helix::json_util::safe_string(slots[s], "material", "");
                    if (slots[s].contains("color")) {
                        st.color_rgb[s] = color_array_to_rgb(slots[s]["color"]);
                    }
                }
            }
            changed = true;
        }
    }

    rebuild_ace_units_locked();
}

void AmsBackendMultiAce::rebuild_ace_units_locked() {
    // Unit 0 is the U1 and is owned by the base class — never touched here.
    // Units 1..device_count_ mirror the ACE hardware. Rebuilt wholesale rather
    // than patched: device_count can change when a unit is plugged or unplugged,
    // and a stale trailing unit would keep drawing a device that is gone.
    if (system_info_.units.empty()) {
        return;
    }
    system_info_.units.resize(1);

    int next_global = NUM_TOOLS;
    for (int a = 0; a < device_count_; ++a) {
        const auto& st = ace_units_[a];
        AmsUnit unit;
        unit.unit_index = a + 1;
        unit.name = fmt::format("ACE{}", a);
        // With one unit the model is the most useful label. With several, which
        // unit it is matters more than which model, and the card is far too
        // narrow at 480x320 to carry both — the model stays available on the
        // unit itself for the detail view.
        const char* model = ace_model_name(st.protocol);
        unit.display_name = device_count_ > 1 ? fmt::format("ACE {}", a + 1) : model;
        unit.firmware_version = model;
        unit.slot_count = ACE_SLOTS_PER_UNIT;
        unit.first_slot_global_index = next_global;
        unit.connected = st.connected;
        // In head mode one ACE binds to a single head and all four of its slots
        // feed that head — a hub, not a parallel fan. In multi mode slot s feeds
        // head s, so each slot has its own path.
        unit.topology = head_mode_ ? PathTopology::HUB : PathTopology::PARALLEL;
        if (st.temp != 0 || st.humidity != 0) {
            EnvironmentData env;
            env.temperature_c = static_cast<float>(st.temp);
            env.humidity_pct = static_cast<float>(st.humidity);
            env.has_humidity = true;
            unit.environment = env;
        }

        for (int s = 0; s < ACE_SLOTS_PER_UNIT; ++s) {
            SlotInfo slot;
            slot.slot_index = s;
            slot.global_index = next_global++;
            // gate_status is the ACE's own "is there filament at this bay",
            // which is the only presence signal it publishes for an idle slot —
            // slots[].status reads "unknown" on real hardware even when the
            // gate is occupied.
            slot.status = st.gate_present[s] ? SlotStatus::AVAILABLE : SlotStatus::EMPTY;
            slot.material = st.material[s];
            if (st.color_rgb[s]) {
                slot.color_rgb = *st.color_rgb[s];
            }
            // Which head this slot can reach. In head mode that is whichever
            // head this ACE is bound to; in multi mode it is the same-numbered
            // head. -1 when nothing is bound, so the canvas draws no lane.
            slot.mapped_tool = -1;
            if (head_mode_) {
                for (int h = 0; h < NUM_TOOLS; ++h) {
                    if (head_seated_[h] && head_seated_[h]->ace_index == a) {
                        slot.mapped_tool = h;
                        break;
                    }
                }
            } else if (s < NUM_TOOLS) {
                slot.mapped_tool = s;
            }
            unit.slots.push_back(slot);
        }
        system_info_.units.push_back(std::move(unit));
    }

    system_info_.total_slots = next_global;
    spdlog::info("{} {} ACE unit(s) in {} mode -> {} units, {} slots total", backend_log_tag(),
                 device_count_, head_mode_ ? "head" : "multi", system_info_.units.size(),
                 system_info_.total_slots);
}

// ============================================================================
// Filament ops — ACE-fed heads take the ACE path
// ============================================================================

AmsError AmsBackendMultiAce::do_load_filament(int slot_index) {
    auto err = validate_slot_index(slot_index);
    if (err.result != AmsResult::SUCCESS) {
        return err;
    }
    if (head_kind_[slot_index] != HeadSource::ACE) {
        // Stock feeder (or not yet known): the inherited native path is right,
        // and is the only one that exists for a head the ACE does not own.
        return AmsBackendSnapmaker::do_load_filament(slot_index);
    }
    spdlog::info("[AmsBackendMultiAce] ACE-fed head {} -> ACE_LOAD_HEAD", slot_index);
    return execute_gcode(fmt::format("ACE_LOAD_HEAD HEAD={}", slot_index));
}

AmsError AmsBackendMultiAce::do_unload_filament(int slot_index) {
    // Callers may pass -1 for "whatever is loaded"; resolve it the same way the
    // base does before deciding which path owns the head.
    int head = slot_index;
    if (head < 0) {
        head = system_info_.current_slot;
    }
    if (head < 0 || head >= NUM_TOOLS || head_kind_[head] != HeadSource::ACE) {
        return AmsBackendSnapmaker::do_unload_filament(slot_index);
    }
    // The native unload does move the filament on an ACE-fed head, but it
    // terminates at preload_finish because the ACE performs the retract — the
    // UI waits for unload_finish and sits on "Unloading" forever (plan §10.2).
    spdlog::info("[AmsBackendMultiAce] ACE-fed head {} -> ACE_UNLOAD_HEAD", head);
    return execute_gcode(fmt::format("ACE_UNLOAD_HEAD HEAD={}", head));
}
