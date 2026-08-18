// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ams_backend_multiace.h"

#include "color_utils.h"
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

/// Split multiACE's `"<ace>_<slot>"` bay key, bounds-checked.
///
/// Used by BOTH `slot_overrides.json` and the `spool_binding` map — they share
/// the key shape, and the two hand-written copies of this had already drifted
/// apart in their comments. Returns nullopt for a malformed key or one naming
/// hardware outside MAX_ACE_UNITS / ACE_SLOTS_PER_UNIT, so callers `continue`.
std::optional<std::pair<int, int>> parse_bay_key(const std::string& key, int max_aces,
                                                 int slots_per_unit) {
    const auto sep = key.find('_');
    if (sep == std::string::npos) {
        return std::nullopt;
    }
    int a = -1;
    int s = -1;
    try {
        a = std::stoi(key.substr(0, sep));
        s = std::stoi(key.substr(sep + 1));
    } catch (...) {
        return std::nullopt;
    }
    if (a < 0 || a >= max_aces || s < 0 || s >= slots_per_unit) {
        return std::nullopt;
    }
    return std::pair<int, int>{a, s};
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

int AmsBackendMultiAce::head_fed_by_ace_locked(int ace_index) const {
    for (int h = 0; h < NUM_TOOLS; ++h) {
        if (head_kind_[h] == HeadSource::ACE && head_ace_index_[h] == ace_index) {
            return h;
        }
    }
    return -1;
}

int AmsBackendMultiAce::bay_feeds_head_locked(int ace_index, int bay) const {
    if (ace_index < 0 || ace_index >= MAX_ACE_UNITS || bay < 0 || bay >= ACE_SLOTS_PER_UNIT) {
        return -1;
    }
    if (!head_mode_) {
        // Multi mode: bay s feeds head s. Nothing to look up -- multiACE's own
        // web UI sends `ACE_LOAD_HEAD HEAD=<slot> ACE=<ace>` here, and the
        // firmware's multi-mode branch defaults SLOT to HEAD for the same reason.
        return bay < NUM_TOOLS ? bay : -1;
    }
    // Head mode: the one head this ACE is bound to. From head_ace -- NOT from
    // head_seated_, which empties on every unload and took the bays' tool badges
    // and the unit's path with it. Seating is only the fallback for firmware
    // that has not sent the wiring map yet; it is the only other thing that
    // knows which head the ACE reaches.
    const int wired = head_fed_by_ace_locked(ace_index);
    if (wired >= 0) {
        return wired;
    }
    for (int h = 0; h < NUM_TOOLS; ++h) {
        if (head_seated_[h] && head_seated_[h]->ace_index == ace_index) {
            return h;
        }
    }
    return -1;
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

    // The SAME unwrapping the base just did -- literally, the shared helper --
    // so the U1 half and the ACE half of one frame can never parse different
    // objects. (Reaching for a "status" key instead finds neither the wrapped
    // notification nor the bare initial query, and the backend then starts,
    // subscribes, and silently behaves like the plain Snapmaker one.)
    const nlohmann::json* status_ptr = unwrap_status_notification(notification);
    if (!status_ptr) {
        return;
    }
    const auto& status = *status_ptr;

    bool changed = false;
    bool want_overrides = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (status.contains("ace") && status["ace"].is_object()) {
            parse_ace_object_locked(status["ace"], changed);
            // Read, NOT cleared: only the fetch that actually goes out consumes
            // the flag. Clearing it here lost every refetch that landed while a
            // download was in flight (see the member comment).
            want_overrides = override_refetch_wanted_;
        }
        // EVERY frame, not just the ones carrying `ace`. The swap phase is
        // derived from the U1's channel_state, and that arrives in
        // `filament_feed` frames which have no `ace` object at all -- gating this
        // on one meant the ACE-side step was never synthesized in practice.
        apply_swap_phase_locked(changed);
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

    // multiACE's own swap telemetry. Cached for corroboration and for the log,
    // NOT branched on: `swap_phase` is a string whose enumeration is unknown --
    // every capture taken so far is "idle" -- and it is not established whether
    // `swap_in_progress` tracks ACE_LOAD_HEAD/ACE_UNLOAD_HEAD or only the
    // single-command ACE_SWAP_HEAD this backend does not use. The ACE-side step
    // is therefore derived from the U1's own channel_state (see
    // apply_swap_phase_locked), which is understood, and these only enrich it.
    // Deliberately does NOT set `changed`: telemetry moving on its own must not
    // force a UI resync (the same reason AceUnitState has whole-state equality).
    if (ace.contains("swap_in_progress") && ace["swap_in_progress"].is_boolean()) {
        ace_swap_in_progress_ = ace["swap_in_progress"].get<bool>();
    }
    ace_swap_phase_ = helix::json_util::safe_string(ace, "swap_phase", ace_swap_phase_);
    ace_status_ = helix::json_util::safe_string(ace, "status", ace_status_);

    // `last_swap_result` is an OBJECT, not a string:
    //   {"head":3, "ace":0, "slot":1, "status":"ok", "ts":6163.707908022}
    // (null before the first swap of a session). Read off a live U1 -- it was
    // written as a string first, and `is_string()` meant the whole branch was
    // simply dead on hardware rather than wrong, which is the harder kind to
    // notice.
    //
    // It is the LAST result and it PERSISTS, so the value alone says nothing
    // about now: surfacing a non-ok status on sight would pop an error for a
    // swap that failed before the UI even started. `ts` is what makes it an
    // event -- record it on the first frame, and only a CHANGE is news.
    const auto& lsr = ace.contains("last_swap_result") ? ace["last_swap_result"] : nlohmann::json();
    if (lsr.is_object()) {
        const double ts = helix::json_util::safe_double(lsr, "ts", 0.0);
        const auto status = helix::json_util::safe_string(lsr, "status", "ok");
        const bool first_seen = last_swap_ts_ < 0.0;
        if (ts != last_swap_ts_) {
            last_swap_ts_ = ts;
            const bool failed = !status.empty() && status != "ok" && status != "success";
            if (failed && !first_seen) {
                const int head = helix::json_util::safe_int(lsr, "head", -1);
                spdlog::warn("{} swap on head {} reported '{}'", backend_log_tag(), head, status);
                system_info_.action = AmsAction::ERROR;
                system_info_.operation_detail = status;
                swap_in_flight_head_ = -1;
                changed = true;
            } else if (failed) {
                spdlog::debug("{} last_swap_result='{}' predates this session — not surfacing",
                              backend_log_tag(), status);
            }
        }
    }

    // Per-head source kind. Order matters and is not arbitrary: `head_ace`
    // carries an ACE index for EVERY head (on the live U1 it reads
    // {0:0,1:1,2:2,3:0} while only head 3 is ACE-fed), so it cannot be used to
    // decide *whether* a head is ACE-fed. head_feeder/head_manual are the
    // authority; ACE is what is left over.
    //
    // Each map is its own DELTA. Klippy diffs `ace` per field, and the two maps
    // are separate fields, so a frame can carry one without the other -- an
    // ACE<->MANUAL toggle resends `head_manual` while `head_feeder`, unchanged,
    // is omitted. Deciding from the frame's own maps read the absent one as
    // all-false and relabelled the stock-feeder heads ACE-fed, which is the
    // wrong command path for them and made a native unload end at
    // preload_finish. So: merge whichever maps this frame carries into the
    // cached per-head values, then decide from the cache. A frame that carries
    // neither map leaves the kinds alone (a partial update used to re-run the
    // decision against two empty maps, with the same relabelling).
    const bool have_manual = ace.contains("head_manual") && ace["head_manual"].is_object();
    const bool have_feeder = ace.contains("head_feeder") && ace["head_feeder"].is_object();
    const bool have_maps = have_manual || have_feeder;
    if (have_manual) {
        for (int h = 0; h < NUM_TOOLS; ++h) {
            head_manual_[h] = head_keyed<bool>(ace["head_manual"], h).value_or(false);
        }
    }
    if (have_feeder) {
        for (int h = 0; h < NUM_TOOLS; ++h) {
            head_feeder_[h] = head_keyed<bool>(ace["head_feeder"], h).value_or(false);
        }
    }

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
            if (head_manual_[h].value_or(false)) {
                kind = HeadSource::MANUAL;
            } else if (head_feeder_[h].value_or(false)) {
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
            // Compared at the end of the loop body. Klippy resends the whole
            // `aces` array whenever ONE element's field ticks, so "an entry was
            // present" is not "an entry changed" -- marking every entry changed
            // unconditionally forced a full AmsState resync and UI rebuild per
            // frame for the length of every dry cycle's temperature ramp.
            const AceUnitState before = st;
            st.connected = helix::json_util::safe_bool(unit, "connected", st.connected);
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
                st.auto_dry_enabled =
                    helix::json_util::safe_bool(ad, "enabled", st.auto_dry_enabled);
                st.auto_dry_rh_start =
                    helix::json_util::safe_float(ad, "rh_start", st.auto_dry_rh_start);
                st.auto_dry_rh_end = helix::json_util::safe_float(ad, "rh_end", st.auto_dry_rh_end);
                st.auto_dry_temp_c = helix::json_util::safe_int(ad, "temp", st.auto_dry_temp_c);
                st.auto_dry_master = helix::json_util::safe_int(ad, "master", st.auto_dry_master);
                st.auto_dry_add_time_min =
                    helix::json_util::safe_int(ad, "add_time", st.auto_dry_add_time_min);
            }
            if (unit.contains("auto_dry_running")) {
                st.auto_dry_running =
                    helix::json_util::safe_bool(unit, "auto_dry_running", st.auto_dry_running);
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
            if (st != before) {
                changed = true;
            }
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
    // Only when something moved. Every input the rebuild reads sets `changed`
    // when it changes (mode, count, head kinds/wiring/seating, per-unit state
    // including the spool table -- all compared field-for-field above), and the
    // one input that lives elsewhere, slot_overrides_, rebuilds from its own
    // fetch completion. Rebuilding on every `ace` frame re-formatted the unit
    // names and re-merged three identity layers per bay under mutex_ for
    // frames that carried nothing this parser reads.
    if (changed) {
        rebuild_ace_units_locked();
    }
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
        // Keyed "<ace>_<slot>". The entries also repeat the pair as `ace`/`slot`
        // fields; the key is authoritative.
        const auto bay = parse_bay_key(key, MAX_ACE_UNITS, ACE_SLOTS_PER_UNIT);
        if (!bay) {
            continue;
        }
        const int a = bay->first;
        const int s = bay->second;
        auto& o = out[static_cast<size_t>(a)][static_cast<size_t>(s)];
        o.set = true;
        o.material = helix::json_util::safe_string(v, "material", "");
        o.brand = helix::json_util::safe_string(v, "brand", "");
        // "#RRGGBB" here — leading '#', unlike the spool table's bare hex and
        // unlike slots[].color's [r,g,b] array. Three encodings, one concept.
        // parse_hex_color tolerates the optional leading '#' and validates the
        // WHOLE string — std::stoul would accept a valid prefix and silently
        // turn "12G456" into near-black.
        if (auto rgb = helix::parse_hex_color(helix::json_util::safe_string(v, "color", ""))) {
            o.color_rgb = *rgb;
        }
    }
    return out;
}

void AmsBackendMultiAce::download_slot_overrides(
    std::function<void(const std::string&)> on_success,
    std::function<void(const MoonrakerError&)> on_error) {
    if (!api_) {
        return;
    }
    api_->transfers().download_file("config", "extended/multiace/slot_overrides.json",
                                    std::move(on_success), std::move(on_error));
}

void AmsBackendMultiAce::fetch_slot_overrides() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (override_fetch_in_flight_) {
            // One at a time. The want-flag is deliberately LEFT SET: this frame's
            // event_seq is already recorded as fetched, so no later frame with
            // the same seq will ask again -- the completion handler below is
            // what re-issues the fetch. Returning here used to drop the request
            // on the floor, and the download in flight brought back the file as
            // it was BEFORE the edit that bumped the seq.
            return;
        }
        override_fetch_in_flight_ = true;
        override_refetch_wanted_ = false; // consumed by the fetch going out
    }

    auto token = lifetime_.token();
    download_slot_overrides(
        [this, token](const std::string& content) {
            // BG THREAD: parse_slot_overrides is static and touches no member.
            OverrideMap parsed = parse_slot_overrides(content);
            token.defer("AmsBackendMultiAce::slot_overrides_apply",
                        [this, parsed = std::move(parsed)]() mutable {
                            bool again = false;
                            {
                                std::lock_guard<std::mutex> lock(mutex_);
                                slot_overrides_ = std::move(parsed);
                                override_fetch_in_flight_ = false;
                                again = override_refetch_wanted_;
                                rebuild_ace_units_locked();
                            }
                            emit_event(EVENT_STATE_CHANGED);
                            if (again) {
                                fetch_slot_overrides(); // a seq moved while this was out
                            }
                        });
        },
        [this, token](const MoonrakerError& error) {
            // Absent on an install that has never used the web UI, which is not
            // an error worth shouting about — the spool table still stands.
            spdlog::debug("[AMS multiACE] slot_overrides.json unavailable: {}", error.message);
            token.defer("AmsBackendMultiAce::slot_overrides_failed", [this]() {
                bool again = false;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    override_fetch_in_flight_ = false;
                    again = override_refetch_wanted_;
                }
                if (again) {
                    fetch_slot_overrides();
                }
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
            if (auto rgb = helix::parse_hex_color(helix::json_util::safe_string(sp, "color", ""))) {
                e.color_rgb = *rgb;
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
            const auto bay = parse_bay_key(key, MAX_ACE_UNITS, ACE_SLOTS_PER_UNIT);
            if (!bay) {
                continue;
            }
            const int a = bay->first;
            const int s = bay->second;
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
            // Whole-struct compare: the field list this used to spell out
            // omitted `vendor`, so a brand-only correction never emitted.
            if (cur != nx) {
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
            // LOADED when this exact bay is the one currently feeding a head:
            // without it a bay the user just loaded looked identical to its
            // three idle neighbours, and every "is this loaded?" rule the UI
            // asks -- the spool highlight, the Unload entry -- answered no.
            // head_seated_ is the right source here (unlike mapped_tool below,
            // which must survive an unload): "which bay is AT the head" is
            // exactly what it tracks.
            bool bay_is_seated = false;
            for (int h = 0; h < NUM_TOOLS && !bay_is_seated; ++h) {
                bay_is_seated = head_seated_[h] && head_seated_[h]->ace_index == a &&
                                head_seated_[h]->slot == s;
            }
            slot.status = bay_is_seated        ? SlotStatus::LOADED
                          : st.gate_present[s] ? SlotStatus::AVAILABLE
                                               : SlotStatus::EMPTY;
            slot.material = st.material[s];
            if (st.color_rgb[s]) {
                slot.color_rgb = *st.color_rgb[s];
            }
            // Which head this slot can reach; -1 when nothing is bound, so the
            // canvas draws no lane. THE rule, shared with bay_source() so the
            // badge and the dispatch can never name different heads.
            slot.mapped_tool = bay_feeds_head_locked(a, s);

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

AmsBackend::OperationStepModel
AmsBackendMultiAce::get_operation_step_model(StepOperationType op) const {
    // Which head the operation is about.
    //
    // op_target_head_ is the head the last dispatch NAMED; current_slot -- the
    // head on the carriage -- is the fallback for an operation this backend did
    // not dispatch. Preferring the target matters in `mode="multi"`, where bay
    // *s* feeds head *s* and the bay being acted on routinely belongs to a head
    // other than the mounted one; asking current_slot there read the wrong
    // head's source kind. One lock scope so the reads are one snapshot, and the
    // _locked helpers exist so nothing here re-takes the non-recursive mutex_.
    bool ace_fed = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        int head = op_target_head_ >= 0 ? op_target_head_ : system_info_.current_slot;
        if (head >= NUM_TOOLS) {
            if (auto bay = bay_source_locked(head)) {
                head = bay->head;
            }
        }
        ace_fed = head >= 0 && head < NUM_TOOLS && head_kind_[head] == HeadSource::ACE;
    }

    // A swap on an ACE-fed head is ONE operation with two halves and an ACE-side
    // gap between them, so it gets a model of its own rather than the load model
    // the base answers with. The ids are what make it work: unload states carry
    // UNLOAD_PHASE_BASE ids, load states LOAD_PHASE_BASE ids, and the sidebar
    // resolves ids through this list instead of indexing by them.
    //
    // The load half's own Home / Select / Heat (LOAD_PHASE_BASE + 0..2) are
    // deliberately absent: by then the head is already mounted and already hot
    // from steps 2-3, so they pass straight through, and an id this model does
    // not declare holds the bar rather than moving it backwards.
    //
    // There is NO step for the ACE-side fetch, and that is a hardware finding
    // rather than an omission. The design assumed a long blind window between
    // the halves -- the ACE spooling the new bay down to the U1's gear with
    // nothing on channel_state. Measured on a live U1 (firmware 20260722) it is
    // a ~4 second blip of `inited` in a ~100 second operation, because the bay
    // is already staged at its gate. A permanent row that is complete before it
    // is read is worse than no row: the bar simply holds on "Retract filament"
    // across it, which is honest.
    //
    // Labels carry no product name, per 99dbe2774 ("Remove specific ACE naming").
    if (op == StepOperationType::LOAD_SWAP && ace_fed) {
        OperationStepModel model;
        model.steps.push_back({lv_tr("Home"), UNLOAD_PHASE_BASE + 0, false, false});
        model.steps.push_back({lv_tr("Select"), UNLOAD_PHASE_BASE + 1, false, false});
        model.steps.push_back(
            {lv_tr("Heat nozzle"), UNLOAD_PHASE_BASE + 2, false, /*live_temp=*/true});
        model.steps.push_back({lv_tr("Retract filament"), UNLOAD_PHASE_BASE + 3, false, false});
        model.steps.push_back({lv_tr("Feed filament"), LOAD_PHASE_BASE + 3, false, false});
        model.steps.push_back({lv_tr("Purge"), LOAD_PHASE_BASE + 4, false, false});
        return model;
    }

    AmsBackend::OperationStepModel model = AmsBackendSnapmaker::get_operation_step_model(op);
    if (op != StepOperationType::UNLOAD || model.steps.empty() || !ace_fed) {
        return model; // a stock feeder head retracts to its own buffer
    }
    model.steps.back().label = lv_tr("Retract filament");
    return model;
}

void AmsBackendMultiAce::apply_swap_phase_locked(bool& changed) {
    (void)changed;
    // Nothing to do unless a swap this backend dispatched is in flight.
    if (swap_in_flight_head_ < 0) {
        return;
    }

    // The pre-start lag has to be survived before an idle action can mean
    // "over". do_load_filament() arms the latch when the gcode goes out, and the
    // firmware has not picked it up yet -- the next frame still reports IDLE. A
    // bare `action == IDLE` disarm therefore fired on the very first frame after
    // dispatch, every time, which is the same trap OperationOwnership documents
    // on the UI side. Only a running action makes a later idle one mean the end.
    const bool running = system_info_.action == AmsAction::LOADING ||
                         system_info_.action == AmsAction::UNLOADING ||
                         system_info_.action == AmsAction::HEATING;
    if (running) {
        swap_progress_seen_ = true;
    }

    // The load half has begun, or the operation has ended: the swap is over as
    // far as this latch is concerned. Disarming on the load half rather than on
    // load_finish gives preload_finish its normal meaning back the moment it can
    // no longer be the boundary between the two halves.
    const int phase = system_info_.operation_phase;
    const bool load_half_started = phase >= LOAD_PHASE_BASE;
    const bool operation_over =
        swap_progress_seen_ && (system_info_.action == AmsAction::IDLE ||
                                system_info_.action == AmsAction::ERROR);
    if (load_half_started || operation_over) {
        spdlog::debug("{} swap on head {} done (phase={} action={})", backend_log_tag(),
                      swap_in_flight_head_, phase, ams_action_to_string(system_info_.action));
        swap_in_flight_head_ = -1;
        swap_progress_seen_ = false;
    }
}

bool AmsBackendMultiAce::bay_load_needs_unload_locked(int slot_index) const {
    const auto bay = bay_source_locked(slot_index);
    if (!bay || bay->head < 0) {
        return false; // a head, an out-of-range index, or a bay bound to nothing
    }
    const auto& seated = head_seated_[static_cast<size_t>(bay->head)];
    if (!seated) {
        return false; // nothing at that head to retract
    }
    // Already the seated bay: ACE_LOAD_HEAD is a no-op re-feed, not a swap.
    return !(seated->ace_index == bay->ace_index && seated->slot == bay->bay);
}

bool AmsBackendMultiAce::needs_unload_before_load(const AmsSystemInfo& info,
                                                  int target_slot) const {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (bay_source_locked(target_slot)) {
            return bay_load_needs_unload_locked(target_slot);
        }
    }
    // Heads and feeder lanes keep the inherited answer. Delegated with the lock
    // DROPPED: the base reaches slot_has_independent_path() -> get_unit_topology(),
    // which this class overrides and which takes mutex_ (ams_backend.h records
    // the same hazard for AFC).
    return AmsBackendSnapmaker::needs_unload_before_load(info, target_slot);
}

std::optional<AmsBackendMultiAce::BaySource>
AmsBackendMultiAce::bay_source_locked(int slot_index) const {
    if (slot_index < NUM_TOOLS) {
        return std::nullopt; // a head, not a bay
    }
    // system_info_ is the authority on where a unit's slots start -- deriving it
    // as NUM_TOOLS + ace_index * ACE_SLOTS_PER_UNIT would silently drift if the
    // unit list is ever built differently (same reasoning as
    // slot_identity_owner_slot()).
    for (size_t u = 1; u < system_info_.units.size(); ++u) {
        const auto& unit = system_info_.units[u];
        const int first = unit.first_slot_global_index;
        if (slot_index < first || slot_index >= first + unit.slot_count) {
            continue;
        }
        BaySource src;
        src.ace_index = static_cast<int>(u) - 1; // unit 0 is the U1 itself
        src.bay = slot_index - first;
        src.head = bay_feeds_head_locked(src.ace_index, src.bay);
        return src;
    }
    return std::nullopt;
}

std::optional<AmsBackendMultiAce::BaySource> AmsBackendMultiAce::bay_source(int slot_index) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return bay_source_locked(slot_index);
}

bool AmsBackendMultiAce::slot_is_actively_loaded(int slot_index) const {
    {
        // One lock, no SlotInfo copy. This runs for EVERY slot on every frame
        // (AmsState::sync_from_backend), and it used to take mutex_ twice --
        // once in bay_source(), once in get_slot_info() -- and deep-copy a
        // SlotInfo with its ~8 std::strings to read a single enum.
        //
        // rebuild_ace_units_locked() marks exactly the seated bay LOADED, so the
        // status IS the answer; this stays one source of truth rather than a
        // second reverse lookup that could disagree.
        std::lock_guard<std::mutex> lock(mutex_);
        if (slot_index >= NUM_TOOLS) {
            const SlotInfo* slot = system_info_.get_slot_global(slot_index);
            return slot != nullptr && slot->status == SlotStatus::LOADED;
        }
    }
    return AmsBackendSnapmaker::slot_is_actively_loaded(slot_index);
}

AmsError AmsBackendMultiAce::do_load_filament(int slot_index) {
    // A BAY, not a head: this is "feed this specific spool to the head its ACE
    // serves". The base validate_slot_index() only knows the U1's four heads, so
    // this arm comes first. Sequence copied from multiACE's own web UI
    // (`http://<printer>/multiace/app.js`, loadSlotHeadMode): unload the head
    // first when it already holds a DIFFERENT source, then load, because
    // ACE_LOAD_HEAD's own guard only refuses when filament is already at the
    // head -- it will not swap for you.
    // One snapshot of everything the decision reads -- the bay's head, what is
    // seated there, the head's kind -- rather than three lock round-trips that
    // could straddle a frame. The gcode goes out with the lock dropped.
    std::optional<BaySource> bay;
    std::optional<SeatedSource> seated;
    HeadSource head_kind = HeadSource::UNKNOWN;
    bool needs_unload = false;
    int last_slot = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        bay = bay_source_locked(slot_index);
        last_slot = system_info_.total_slots - 1;
        if (bay && bay->head >= 0) {
            seated = head_seated_[bay->head];
            // The SAME predicate needs_unload_before_load() answers the planner
            // with, so the bar the UI draws and the commands that go out are one
            // decision. They used to be two hand-written copies of it.
            needs_unload = bay_load_needs_unload_locked(slot_index);
            op_target_head_ = bay->head;
            // Arm BEFORE the gcode goes out: the two commands are queued back to
            // back and the first channel_state can land before we return.
            swap_in_flight_head_ = needs_unload ? bay->head : -1;
        } else if (!bay && slot_index >= 0 && slot_index < NUM_TOOLS) {
            head_kind = head_kind_[slot_index];
            op_target_head_ = slot_index;
            swap_in_flight_head_ = -1;
        }
    }

    if (bay) {
        if (bay->head < 0) {
            return AmsErrorHelper::invalid_slot(slot_index, last_slot);
        }
        if (needs_unload && seated) {
            spdlog::info("[AmsBackendMultiAce] head {} holds ACE {} slot {} -> unload before "
                         "loading ACE {} slot {}",
                         bay->head, seated->ace_index, seated->slot, bay->ace_index, bay->bay);
            AmsError unload_err = execute_gcode(fmt::format("ACE_UNLOAD_HEAD HEAD={}", bay->head));
            if (!unload_err.success()) {
                // No swap is running after all — disarm, or preload_finish would
                // stop resolving unloads on this head for the rest of the session.
                std::lock_guard<std::mutex> lock(mutex_);
                swap_in_flight_head_ = -1;
                return unload_err;
            }
        }
        spdlog::info("[AmsBackendMultiAce] bay {} -> ACE_LOAD_HEAD HEAD={} ACE={} SLOT={}",
                     slot_index, bay->head, bay->ace_index, bay->bay);
        AmsError load_err = execute_gcode(fmt::format("ACE_LOAD_HEAD HEAD={} ACE={} SLOT={}",
                                                      bay->head, bay->ace_index, bay->bay));
        if (!load_err.success()) {
            std::lock_guard<std::mutex> lock(mutex_);
            swap_in_flight_head_ = -1;
        }
        return load_err;
    }

    auto err = validate_slot_index(slot_index);
    if (err.result != AmsResult::SUCCESS) {
        return err;
    }
    if (head_kind != HeadSource::ACE) {
        // Stock feeder (or not yet known): the inherited native path is right,
        // and is the only one that exists for a head the ACE does not own.
        return AmsBackendSnapmaker::do_load_filament(slot_index);
    }
    spdlog::info("[AmsBackendMultiAce] ACE-fed head {} -> ACE_LOAD_HEAD", slot_index);
    return execute_gcode(fmt::format("ACE_LOAD_HEAD HEAD={}", slot_index));
}

AmsError AmsBackendMultiAce::do_unload_filament(int slot_index) {
    // A BAY: "unload the head this spool is feeding". Same shape as the load
    // arm in do_load_filament() and for the same reason -- the base
    // validate_slot_index() only knows the U1's four heads, so a bay index
    // would fall through to the native path and be refused as out of range.
    // ACE_UNLOAD_HEAD names only the head; the bay is implied by what is seated.
    std::optional<BaySource> bay;
    int last_slot = 0;
    // Callers may pass -1 for "whatever is loaded"; resolve it the same way the
    // base does before deciding which path owns the head.
    int head = slot_index;
    HeadSource head_kind = HeadSource::UNKNOWN;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        bay = bay_source_locked(slot_index);
        last_slot = system_info_.total_slots - 1;
        if (head < 0) {
            head = system_info_.current_slot;
        }
        if (head >= 0 && head < NUM_TOOLS) {
            head_kind = head_kind_[head];
        }
        // A standalone unload, whichever entry point asked for it: name the head
        // for the step model, and make sure no stale swap latch survives into it.
        op_target_head_ = bay && bay->head >= 0 ? bay->head : head;
        swap_in_flight_head_ = -1;
    }

    if (bay) {
        if (bay->head < 0) {
            return AmsErrorHelper::invalid_slot(slot_index, last_slot);
        }
        spdlog::info("[AmsBackendMultiAce] bay {} -> ACE_UNLOAD_HEAD HEAD={}", slot_index,
                     bay->head);
        return execute_gcode(fmt::format("ACE_UNLOAD_HEAD HEAD={}", bay->head));
    }

    if (head < 0 || head >= NUM_TOOLS || head_kind != HeadSource::ACE) {
        return AmsBackendSnapmaker::do_unload_filament(slot_index);
    }
    // The native unload does move the filament on an ACE-fed head, but it
    // terminates at preload_finish because the ACE performs the retract — the
    // UI waits for unload_finish and sits on "Unloading" forever (plan §10.2).
    spdlog::info("[AmsBackendMultiAce] ACE-fed head {} -> ACE_UNLOAD_HEAD", head);
    return execute_gcode(fmt::format("ACE_UNLOAD_HEAD HEAD={}", head));
}
