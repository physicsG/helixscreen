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
template <typename T> std::optional<T> head_keyed(const nlohmann::json& obj, int head) {
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
        return c[i].is_number_integer() ? static_cast<uint32_t>(std::clamp(c[i].get<int>(), 0, 255))
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

AmsBackendMultiAce::AmsBackendMultiAce(IMoonrakerAPI* api, helix::IMoonrakerClient* client)
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
    if (head_kind_[slot_index] != HeadSource::ACE) {
        return std::nullopt;
    }
    // Wiring, not contents: an ACE-fed head is the ACE's to describe whether or
    // not filament happens to be in it. Requiring head_seated_ here made the head
    // flip back to a SnapSwap spool position on every unload — the unit count
    // went 3 -> 4 and the ACE's bays renumbered under the user.
    if (head_ace_index_[slot_index] >= 0) {
        return head_ace_index_[slot_index] + 1; // unit 0 is the U1 itself
    }
    // No head_ace yet (older firmware, or before the first full frame): fall
    // back to whatever is seated, which is the only other thing that knows.
    if (head_seated_[slot_index]) {
        return head_seated_[slot_index]->unit_index;
    }
    return std::nullopt;
}

std::optional<int> AmsBackendMultiAce::slot_identity_owner_slot(int slot_index) const {
    std::lock_guard<std::mutex> lock(mutex_);
    // Same gate as slot_identity_owner_unit: only a seated ACE-fed head views a
    // spool it does not hold.
    if (slot_index < 0 || slot_index >= NUM_TOOLS) {
        return std::nullopt;
    }
    if (head_kind_[slot_index] != HeadSource::ACE || !head_seated_[slot_index]) {
        return std::nullopt;
    }
    const auto& seated = *head_seated_[slot_index];
    // system_info_ is the authority on where a unit's slots start; deriving it
    // as NUM_TOOLS + ace_index * ACE_SLOTS_PER_UNIT would silently drift if the
    // unit list is ever built differently.
    if (seated.unit_index < 0 || seated.unit_index >= static_cast<int>(system_info_.units.size())) {
        return std::nullopt;
    }
    return system_info_.units[static_cast<size_t>(seated.unit_index)].first_slot_global_index +
           seated.slot;
}

AmsBackendMultiAce::HeadSource AmsBackendMultiAce::head_source_kind(int head) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (head < 0 || head >= NUM_TOOLS) {
        return HeadSource::UNKNOWN;
    }
    return head_kind_[head];
}

std::optional<AmsBackendMultiAce::SeatedSource> AmsBackendMultiAce::seated_source(int head) const {
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
    bool want_overrides = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        parse_ace_object_locked(status["ace"], changed);
        want_overrides = override_refetch_wanted_;
        override_refetch_wanted_ = false;
    }
    // Outside the lock on purpose: fetch_slot_overrides() takes mutex_ itself.
    if (want_overrides) {
        fetch_slot_overrides();
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
    const auto& manual =
        ace.contains("head_manual") ? ace["head_manual"] : nlohmann::json::object();
    const auto& feeder =
        ace.contains("head_feeder") ? ace["head_feeder"] : nlohmann::json::object();
    // Ask whether the FRAME carried the keys, not whether the locals are objects:
    // the `: json::object()` fallbacks above are objects too, so testing
    // is_object() was true on every frame. A partial update then re-ran the test
    // below against two empty maps, where "not manual, not feeder" falls through
    // to ACE — quietly relabelling all four heads ACE-fed a moment after a good
    // frame, which is the wrong command path for a head on its stock feeder.
    const bool have_maps = (ace.contains("head_manual") && ace["head_manual"].is_object()) ||
                           (ace.contains("head_feeder") && ace["head_feeder"].is_object());

    // ...but once head_feeder/head_manual have decided WHETHER, `head_ace` is the
    // authority on WHICH ACE feeds the head, and it is the only one that survives
    // an EMPTY head: head_source goes null the moment the filament leaves, while
    // the wiring does not change. Binding on head_source alone made an ACE-fed
    // head revert to a SnapSwap spool position whenever it was unloaded.
    const auto& head_ace = ace.contains("head_ace") ? ace["head_ace"] : nlohmann::json::object();

    // Status frames are DELTAS. A frame that says nothing about head sources must
    // leave them alone -- treating "absent" as "cleared" wiped the seating on
    // every partial `ace` update, which on hardware meant the ACE's bays lost
    // their mapped_tool a second after gaining it: no tool badges, and the unit
    // detail fell back to hub-only because it no longer knew which head it fed.
    const bool have_source_map = ace.contains("head_source") && ace["head_source"].is_object();

    for (int h = 0; h < NUM_TOOLS; ++h) {
        if (have_maps) {
            HeadSource kind;
            if (head_keyed<bool>(manual, h).value_or(false)) {
                kind = HeadSource::MANUAL;
            } else if (head_keyed<bool>(feeder, h).value_or(false)) {
                kind = HeadSource::FEEDER;
            } else {
                kind = HeadSource::ACE;
            }
            if (kind != head_kind_[h]) {
                head_kind_[h] = kind;
                changed = true;
            }
        }

        // Delta-safe: only touched when this frame carries the key.
        if (auto a = head_keyed<int>(head_ace, h)) {
            const int idx = (*a >= 0 && *a < MAX_ACE_UNITS) ? *a : -1;
            if (idx != head_ace_index_[h]) {
                head_ace_index_[h] = idx;
                changed = true;
            }
        }

        if (!have_source_map) {
            continue; // nothing said about seating this frame
        }
        std::optional<SeatedSource> seated;
        auto it = ace["head_source"].find(std::to_string(h));
        if (it != ace["head_source"].end() && it->is_object()) {
            const int ai = helix::json_util::safe_int(*it, "ace_index", -1);
            const int sl = helix::json_util::safe_int(*it, "slot", -1);
            if (ai >= 0 && ai < MAX_ACE_UNITS && sl >= 0) {
                seated = SeatedSource{/*unit_index=*/ai + 1, ai, sl};
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

            if (unit.contains("dryer_status") && unit["dryer_status"].is_object()) {
                const auto& ds = unit["dryer_status"];
                // "stop" is the idle value; anything else is a running programme.
                const auto status = helix::json_util::safe_string(ds, "status", "stop");
                st.drying = (status != "stop" && !status.empty());
                st.dryer_target_c = helix::json_util::safe_int(ds, "target_temp", 0);
                st.dryer_duration_min = helix::json_util::safe_int(ds, "duration", 0);
                st.dryer_remaining_min = helix::json_util::safe_int(ds, "remain_time", 0);
            }

            // Frames are DELTAS, so every field keeps its previous value when the
            // frame does not mention it. `auto_dry_running` is its own key next to
            // the block, not inside it, and arrives on its own often enough that
            // defaulting it to false here would flicker the rule off between frames.
            if (unit.contains("auto_dry") && unit["auto_dry"].is_object()) {
                const auto& ad = unit["auto_dry"];
                st.has_auto_dry = true;
                st.auto_dry_enabled = ad.value("enabled", st.auto_dry_enabled);
                st.auto_dry_rh_start =
                    helix::json_util::safe_float(ad, "rh_start", st.auto_dry_rh_start);
                st.auto_dry_rh_end = helix::json_util::safe_float(ad, "rh_end", st.auto_dry_rh_end);
                st.auto_dry_temp_c = helix::json_util::safe_int(ad, "temp", st.auto_dry_temp_c);
                st.auto_dry_master = helix::json_util::safe_int(ad, "master", st.auto_dry_master);
                st.auto_dry_add_time_min =
                    helix::json_util::safe_int(ad, "add_time", st.auto_dry_add_time_min);
            }
            if (unit.contains("auto_dry_running")) {
                st.auto_dry_running = unit.value("auto_dry_running", st.auto_dry_running);
            }

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

    // slot_overrides.json lives on disk, not in this object, so nothing here can
    // say it changed. `event_seq` is multiACE's own bump-on-any-state-change
    // counter, which is the closest thing to a signal — and the first frame
    // (seq -1) is what fetches it at all.
    if (ace.contains("event_seq") && ace["event_seq"].is_number_integer()) {
        const int64_t seq = ace["event_seq"].get<int64_t>();
        if (seq != overrides_fetched_seq_) {
            overrides_fetched_seq_ = seq;
            override_refetch_wanted_ = true;
        }
    } else if (overrides_fetched_seq_ < 0) {
        overrides_fetched_seq_ = 0; // firmware without event_seq: fetch once
        override_refetch_wanted_ = true;
    }

    parse_spool_table_locked(ace, changed);
    rebuild_ace_units_locked();
}

AmsBackendMultiAce::OverrideMap
AmsBackendMultiAce::parse_slot_overrides(const std::string& content) {
    OverrideMap out{};
    nlohmann::json doc;
    try {
        doc = nlohmann::json::parse(content);
    } catch (const nlohmann::json::parse_error& e) {
        spdlog::warn("[AMS multiACE] slot_overrides.json parse error: {}", e.what());
        return out;
    }
    if (!doc.is_object()) {
        return out;
    }
    for (const auto& [key, v] : doc.items()) {
        if (!v.is_object()) {
            continue;
        }
        // Keyed "<ace>_<slot>", the same shape as spool_binding. The entries also
        // repeat the pair as `ace`/`slot` fields; the key is authoritative.
        const auto sep = key.find('_');
        if (sep == std::string::npos) {
            continue;
        }
        int a = -1;
        int s = -1;
        try {
            a = std::stoi(key.substr(0, sep));
            s = std::stoi(key.substr(sep + 1));
        } catch (...) {
            continue;
        }
        if (a < 0 || a >= MAX_ACE_UNITS || s < 0 || s >= ACE_SLOTS_PER_UNIT) {
            continue;
        }
        auto& o = out[static_cast<size_t>(a)][static_cast<size_t>(s)];
        o.set = true;
        o.material = helix::json_util::safe_string(v, "material", "");
        o.brand = helix::json_util::safe_string(v, "brand", "");
        // "#RRGGBB" here — leading '#', unlike the spool table's bare hex and
        // unlike slots[].color's [r,g,b] array. Three encodings, one concept.
        std::string col = helix::json_util::safe_string(v, "color", "");
        if (!col.empty() && col.front() == '#') {
            col.erase(0, 1);
        }
        if (col.size() >= 6) {
            try {
                o.color_rgb =
                    static_cast<uint32_t>(std::stoul(col.substr(col.size() - 6), nullptr, 16));
            } catch (...) {
            }
        }
    }
    return out;
}

void AmsBackendMultiAce::fetch_slot_overrides() {
    if (!api_) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (override_fetch_in_flight_) {
            return; // one at a time; the next event_seq will ask again
        }
        override_fetch_in_flight_ = true;
    }

    auto token = lifetime_.token();
    api_->transfers().download_file(
        "config", "extended/multiace/slot_overrides.json",
        [this, token](const std::string& content) {
            // BG THREAD: parse_slot_overrides is static and touches no member.
            OverrideMap parsed = parse_slot_overrides(content);
            token.defer("AmsBackendMultiAce::slot_overrides_apply",
                        [this, parsed = std::move(parsed)]() mutable {
                            {
                                std::lock_guard<std::mutex> lock(mutex_);
                                slot_overrides_ = std::move(parsed);
                                override_fetch_in_flight_ = false;
                                rebuild_ace_units_locked();
                            }
                            emit_event(EVENT_STATE_CHANGED);
                        });
        },
        [this, token](const MoonrakerError& error) {
            // Absent on an install that has never used the web UI, which is not
            // an error worth shouting about — the spool table still stands.
            spdlog::debug("[AMS multiACE] slot_overrides.json unavailable: {}", error.message);
            token.defer("AmsBackendMultiAce::slot_overrides_failed", [this]() {
                std::lock_guard<std::mutex> lock(mutex_);
                override_fetch_in_flight_ = false;
            });
        });
}

void AmsBackendMultiAce::parse_spool_table_locked(const nlohmann::json& ace, bool& changed) {
    // A bay's identity is NOT in `slots[]` — those fields are filled from RFID
    // and read empty for every hand-entered spool, which is why the panel showed
    // nothing the user had typed into multiACE's own web UI. It lives in a spool
    // TABLE, `spools` (keyed by id, as a string), joined to bays through
    // `spool_binding` with keys of the form "<ace>_<slot>".
    //
    // Both keys are deltas like everything else, so a frame that omits them
    // leaves the table alone. `spool_binding` also has to be handled as a
    // WHOLESALE replacement when present: unbinding a spool DELETES its key
    // rather than nulling it, so merging would strand the old binding forever.
    bool touched = false;

    // The TABLE, keyed by spool id. Cached: a frame that only moves a binding
    // does not resend it, and resolving the two together threw every detail away
    // the moment a spool was unbound.
    if (ace.contains("spools") && ace["spools"].is_object()) {
        spool_table_.clear();
        for (const auto& [id, sp] : ace["spools"].items()) {
            if (!sp.is_object()) {
                continue;
            }
            AceUnitState::BaySpool e;
            e.bound = true;
            e.material = helix::json_util::safe_string(sp, "material", "");
            e.vendor = helix::json_util::safe_string(sp, "vendor", "");
            e.label = helix::json_util::safe_string(sp, "label", "");
            e.sku = helix::json_util::safe_string(sp, "sku", "");
            // spoolman_id is a STRING here, unlike everywhere else it appears.
            const std::string sm = helix::json_util::safe_string(sp, "spoolman_id", "");
            if (!sm.empty()) {
                try {
                    e.spoolman_id = std::stoi(sm);
                } catch (...) {
                    e.spoolman_id = 0;
                }
            }
            // `weight_g` is deliberately not read: it is multiACE's local copy of
            // Spoolman's remaining weight, and Spoolman owns both weights here.
            // Bare hex, no leading '#', unlike slots[].color which is [r,g,b].
            const std::string col = helix::json_util::safe_string(sp, "color", "");
            if (col.size() >= 6) {
                try {
                    e.color_rgb =
                        static_cast<uint32_t>(std::stoul(col.substr(col.size() - 6), nullptr, 16));
                } catch (...) {
                }
            }
            spool_table_[id] = std::move(e);
        }
        touched = true;
    }

    // The BINDINGS. Replaced WHOLESALE when present: unbinding a spool deletes
    // its key rather than nulling it, so merging would strand it forever.
    if (ace.contains("spool_binding") && ace["spool_binding"].is_object()) {
        for (auto& row : bay_spool_id_) {
            row.fill(std::string{});
        }
        for (const auto& [key, value] : ace["spool_binding"].items()) {
            // "<ace>_<slot>"
            const auto sep = key.find('_');
            if (sep == std::string::npos) {
                continue;
            }
            int a = -1;
            int s = -1;
            try {
                a = std::stoi(key.substr(0, sep));
                s = std::stoi(key.substr(sep + 1));
            } catch (...) {
                continue;
            }
            if (a < 0 || a >= MAX_ACE_UNITS || s < 0 || s >= ACE_SLOTS_PER_UNIT) {
                continue;
            }
            // The id is a string in both the binding and the table's keys.
            bay_spool_id_[static_cast<size_t>(a)][static_cast<size_t>(s)] =
                value.is_string()
                    ? value.get<std::string>()
                    : (value.is_number_integer() ? std::to_string(value.get<int>()) : "");
        }
        touched = true;
    }

    if (!touched) {
        return;
    }

    // Resolve bays from the two caches.
    std::array<std::array<AceUnitState::BaySpool, ACE_SLOTS_PER_UNIT>, MAX_ACE_UNITS> next{};
    for (int a = 0; a < MAX_ACE_UNITS; ++a) {
        for (int s = 0; s < ACE_SLOTS_PER_UNIT; ++s) {
            const std::string& id = bay_spool_id_[static_cast<size_t>(a)][static_cast<size_t>(s)];
            if (id.empty()) {
                continue;
            }
            auto& bay = next[static_cast<size_t>(a)][static_cast<size_t>(s)];
            auto it = spool_table_.find(id);
            if (it != spool_table_.end()) {
                bay = it->second;
            } else {
                bay.bound = true; // bound to a spool the table has not described
            }
        }
    }

    for (int a = 0; a < MAX_ACE_UNITS; ++a) {
        for (int s = 0; s < ACE_SLOTS_PER_UNIT; ++s) {
            auto& cur = ace_units_[static_cast<size_t>(a)].spool[static_cast<size_t>(s)];
            const auto& nx = next[static_cast<size_t>(a)][static_cast<size_t>(s)];
            if (cur.bound != nx.bound || cur.material != nx.material || cur.label != nx.label ||
                cur.color_rgb != nx.color_rgb || cur.spoolman_id != nx.spoolman_id) {
                changed = true;
            }
            cur = nx;
        }
    }
}

void AmsBackendMultiAce::rebuild_ace_units_locked() {
    // Unit 0 is the U1 and is owned by the base class — never touched here.
    if (system_info_.units.empty()) {
        return;
    }

    // Grow or shrink ONLY when the hardware count actually changed. This used to
    // resize(1) and push_back fresh units on every `ace` frame, which arrive
    // constantly — so a spool the user had just assigned to an ACE bay was
    // discarded a fraction of a second later ("Slot 5 updated", then nothing),
    // and the churn reset the panel out of the unit detail it was showing.
    const size_t want_units = 1 + static_cast<size_t>(device_count_);
    if (system_info_.units.size() != want_units) {
        system_info_.units.resize(want_units);
    }

    int next_global = NUM_TOOLS;
    for (int a = 0; a < device_count_; ++a) {
        const auto& st = ace_units_[a];
        AmsUnit& unit = system_info_.units[static_cast<size_t>(a) + 1];

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

        if (unit.slots.size() != static_cast<size_t>(ACE_SLOTS_PER_UNIT)) {
            unit.slots.assign(static_cast<size_t>(ACE_SLOTS_PER_UNIT), SlotInfo{});
        }
        for (int s = 0; s < ACE_SLOTS_PER_UNIT; ++s) {
            SlotInfo& slot = unit.slots[static_cast<size_t>(s)];
            const int global_index = next_global++;
            slot.slot_index = s;
            slot.global_index = global_index;
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
                // Which head this ACE is wired to, from head_ace — NOT from
                // head_seated_, which empties on every unload and took the bays'
                // tool badges and the unit's path with it.
                for (int h = 0; h < NUM_TOOLS; ++h) {
                    if (head_kind_[h] == HeadSource::ACE && head_ace_index_[h] == a) {
                        slot.mapped_tool = h;
                        break;
                    }
                }
                if (slot.mapped_tool < 0) {
                    for (int h = 0; h < NUM_TOOLS; ++h) {
                        if (head_seated_[h] && head_seated_[h]->ace_index == a) {
                            slot.mapped_tool = h;
                            break;
                        }
                    }
                }
            } else if (s < NUM_TOOLS) {
                slot.mapped_tool = s;
            }

            // These SlotInfo objects are reused across rebuilds — only a change
            // in unit COUNT reallocates them — so every identity field has to be
            // cleared here or it survives the spool that put it there. `material`
            // and `color_rgb` above are assigned unconditionally and so reset
            // themselves; these four were only ever written, never cleared, and
            // an unbound bay kept the name of the spool taken out of it.
            slot.spool_name.clear();
            slot.brand.clear();
            slot.spoolman_id = 0;
            // Both weights, together: they are one pair and come from one place
            // (Spoolman, via spoolman_id). Clearing only the remaining left a
            // total from a previous spool and a percentage measured against it.
            slot.remaining_weight_g = -1.0f;
            slot.total_weight_g = -1.0f;

            // The base class's convergence point has already run by the time we
            // get here, so these slots would never see the override layer
            // without this — which is why a spool assigned to an ACE bay was
            // written and then invisible.
            apply_overrides_for(slot, global_index);

            // ...but multiACE's own spool table OUTRANKS the local override
            // store for a bay it has a binding for. The ACE is the authority on
            // what is in its own bays — the same doctrine slot_identity_owner_unit()
            // already states for an ACE-fed head — so a value the user typed into
            // multiACE must not be masked by a stale local edit. Applied after
            // the overrides for exactly that reason.
            const auto& bay = st.spool[static_cast<size_t>(s)];
            if (bay.bound) {
                if (!bay.material.empty()) {
                    slot.material = bay.material;
                }
                if (!bay.vendor.empty()) {
                    slot.brand = bay.vendor;
                }
                if (!bay.label.empty()) {
                    slot.spool_name = bay.label;
                }
                if (bay.color_rgb) {
                    slot.color_rgb = *bay.color_rgb;
                }
                // The one the override layer cannot supply, and the one that
                // matters most: SpoolmanManager keys its weight refresh on it and
                // fills BOTH remaining and total from Spoolman. multiACE's own
                // `weight_g` is deliberately NOT used — it is a local copy, so
                // preferring it took remaining from one source and total from
                // another and computed a percentage across the two.
                // tracks_weight_locally() stays false for the same reason.
                if (bay.spoolman_id > 0) {
                    slot.spoolman_id = bay.spoolman_id;
                }
            }

            // The override file wins over both. multiACE resolves its own web UI
            // from it — every bay there reports source:"override" — and it is the
            // only place a bay with NO spool bound can still carry a material and
            // colour, which is how bay 2 read empty here while multiACE showed
            // PETG/Generic.
            const auto& ov = slot_overrides_[static_cast<size_t>(a)][static_cast<size_t>(s)];
            if (ov.set) {
                if (!ov.material.empty()) {
                    slot.material = ov.material;
                }
                if (!ov.brand.empty()) {
                    slot.brand = ov.brand;
                }
                if (ov.color_rgb) {
                    slot.color_rgb = *ov.color_rgb;
                }
                // A bound bay holds a spool even when gate_status has not caught
                // up, and multiACE keeps the binding across a spool being taken
                // out — so presence still comes from the gate, not from this.
            }
        }
    }

    system_info_.total_slots = next_global;
    // debug, not info: this runs on every `ace` frame.
    spdlog::debug("{} {} ACE unit(s) in {} mode -> {} units, {} slots total", backend_log_tag(),
                  device_count_, head_mode_ ? "head" : "multi", system_info_.units.size(),
                  system_info_.total_slots);
}

// ============================================================================
// Dryer
// ============================================================================

namespace {
/// HelixScreen unit 0 is the U1 itself, so ACE `a` is global unit `a + 1`.
/// Returns -1 when the index does not name an ACE.
int ace_index_for_unit(int unit_index, int device_count) {
    const int ace = unit_index - 1;
    return (ace >= 0 && ace < device_count) ? ace : -1;
}
} // namespace

DryerInfo AmsBackendMultiAce::get_dryer_info(int unit) const {
    std::lock_guard<std::mutex> lock(mutex_);
    DryerInfo info;
    const int ace = ace_index_for_unit(unit, device_count_);
    if (ace < 0) {
        return info; // the U1 itself has no dryer
    }
    const auto& st = ace_units_[static_cast<size_t>(ace)];
    info.supported = true;
    // Drying is a chamber operation on the ACE, not a toolhead one, so it is
    // safe while the printer prints.
    info.allows_during_print = true;
    info.active = st.drying;
    info.current_temp_c = static_cast<float>(st.temp);
    info.target_temp_c = static_cast<float>(st.dryer_target_c);
    info.duration_min = st.dryer_duration_min;
    info.remaining_min = st.dryer_remaining_min;
    // The ACE runs its own fan with the heater; there is no independent control.
    info.supports_fan_control = false;
    info.min_temp_c = 35.0f;
    // multiACE's own max_dryer_temperature safety cap.
    info.max_temp_c = 70.0f;
    info.max_duration_min = 12 * 60;
    return info;
}

AmsError AmsBackendMultiAce::start_drying(float temp_c, int duration_min, int fan_pct, int unit) {
    (void)fan_pct; // no independent fan control on the ACE
    int ace = -1;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ace = ace_index_for_unit(unit, device_count_);
    }
    if (ace < 0) {
        return AmsErrorHelper::not_supported("Dryer");
    }
    // ACE_DRY takes TEMP and DURATION directly -- no config edit, no Klipper
    // restart. (ACED__DRY_START_n, the macro in multiACE's Fluidd table, is the
    // parameterless form and would ignore whatever the user picked here.)
    const int temp = std::clamp(static_cast<int>(std::lround(temp_c)), 35, 70);
    const int mins = std::clamp(duration_min, 1, 12 * 60);
    spdlog::info("{} ACE {} dry {} C for {} min", backend_log_tag(), ace, temp, mins);
    return execute_gcode(fmt::format("ACE_DRY ACE={} TEMP={} DURATION={}", ace, temp, mins));
}

AmsError AmsBackendMultiAce::stop_drying(int unit) {
    int ace = -1;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ace = ace_index_for_unit(unit, device_count_);
    }
    if (ace < 0) {
        return AmsErrorHelper::not_supported("Dryer");
    }
    return execute_gcode(fmt::format("ACE_STOP_DRYING ACE={}", ace));
}

AutoDryInfo AmsBackendMultiAce::get_auto_dry_info(int unit) const {
    std::lock_guard<std::mutex> lock(mutex_);
    AutoDryInfo info;
    const int ace = ace_index_for_unit(unit, device_count_);
    if (ace < 0) {
        return info; // the U1 itself has no dryer, so no rule to arm either
    }
    const auto& st = ace_units_[static_cast<size_t>(ace)];
    if (!st.has_auto_dry) {
        return info; // firmware too old to carry the block
    }
    info.supported = true;
    info.enabled = st.auto_dry_enabled;
    info.running = st.auto_dry_running;
    info.rh_start_pct = st.auto_dry_rh_start;
    info.rh_end_pct = st.auto_dry_rh_end;
    info.temp_c = st.auto_dry_temp_c;
    // Only the ACE 2 Pro (protocol v2) carries a humidity sensor, so only it can
    // evaluate a threshold. A v1 unit has no reading of its own and instead
    // mirrors a v2 unit's cycle, running on for add_time past it.
    info.follows_master = (st.protocol != "v2");
    info.add_time_min = st.auto_dry_add_time_min;
    // multiACE names the master by ACE index; AMS unit indices are one higher
    // because unit 0 is the U1. -1 (none picked) must stay -1, not become 0.
    info.master_unit = st.auto_dry_master >= 0 ? st.auto_dry_master + 1 : -1;
    return info;
}

AmsError AmsBackendMultiAce::set_auto_dry_enabled(bool enabled, int unit) {
    // Reads its own lock, so it has to happen before we take ours.
    const AutoDryInfo info = get_auto_dry_info(unit);
    if (!info.supported) {
        return AmsErrorHelper::not_supported("Auto-dry");
    }
    // Arming a follower that has no master is a state multiACE rejects; refusing
    // here keeps the failure on our side of the wire, where it can be explained.
    if (enabled && !info.can_enable()) {
        return AmsErrorHelper::wrong_state("no auto-dry master selected",
                                           "a master unit to follow");
    }
    int ace = -1;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ace = ace_index_for_unit(unit, device_count_);
    }
    if (ace < 0) {
        return AmsErrorHelper::not_supported("Auto-dry");
    }
    spdlog::info("{} ACE {} auto-dry {}", backend_log_tag(), ace, enabled ? "on" : "off");
    // ENABLE alone: every field of ACE_SET_AUTO_DRY is independent, so arming the
    // rule must not restate thresholds the user set in multiACE's own UI.
    return execute_gcode(fmt::format("ACE_SET_AUTO_DRY ACE={} ENABLE={}", ace, enabled ? 1 : 0));
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
