// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ams_backend_snapmaker.h"

#include <array>
#include <map>
#include <optional>
#include <string>

/**
 * @file ams_backend_multiace.h
 * @brief decay71/multiACE — 1-4 Anycubic ACE units bolted onto a Snapmaker U1
 *
 * multiACE does not replace the U1's filament handling, it feeds it. The four
 * SnapSwap toolheads stay exactly what they were — `print_task_config`, the
 * `channel_state` load latch, the motion/port sensors, the native load model —
 * and each head is independently either on its stock feeder or fed from one of
 * the ACE units. So this derives from AmsBackendSnapmaker rather than forking
 * it: unit 0 remains the U1's four heads with all of that behaviour intact, and
 * units 1..N are the ACE hardware.
 *
 * ## What the `ace` Klipper object carries
 *
 * Everything needed arrives over the WebSocket via MultiAce.get_status(); the
 * optional FastAPI service is not required.
 *
 * - `mode` — "head" (an ACE binds to ONE head and all its slots feed that head)
 *   or "multi" (slot *s* of every ACE feeds head *s*). This changes the
 *   slot→head mapping, so per-unit topology genuinely differs by mode.
 * - `device_count`, `active_device`
 * - `head_ace` / `head_feeder` / `head_manual` — per-head source *kind*, keyed
 *   by head index as a STRING ("0".."3"), not an int.
 * - `head_source[h]` — `{ace_index, slot}` when the head is ACE-fed, else null.
 * - `aces[]` — per-unit inventory: connected, temp, humidity, `gate_status[]`,
 *   `slots[]`, dryer state.
 *
 * ## Why the dispatch override exists
 *
 * An ACE-fed head must be driven with `ACE_LOAD_HEAD` / `ACE_UNLOAD_HEAD`, not
 * the U1's native path. Sending the native unload to an ACE-fed head does move
 * the filament, but it terminates at `preload_finish` because the ACE performs
 * the retract — the UI waits for `unload_finish` and hangs on "Unloading"
 * forever. Heads on their stock feeder keep the inherited native path.
 *
 * @see docs/devel/plans/2026-08-09-snapmaker-u1-multiace-plan.md § 4 Phase 2
 * @see tests/fixtures/snapmaker_u1/u1-multiace-head-mode-idle.json
 */
class AmsBackendMultiAce : public AmsBackendSnapmaker {
    friend class MultiAceTestAccess;

  public:
    AmsBackendMultiAce(IMoonrakerAPI* api, helix::IMoonrakerClient* client);
    ~AmsBackendMultiAce() override = default;

    /// Upper bound on ACE units multiACE supports.
    static constexpr int MAX_ACE_UNITS = 4;
    /// Slots per ACE unit (Pro / Pro 2 are both 4-bay).
    static constexpr int ACE_SLOTS_PER_UNIT = 4;

    [[nodiscard]] AmsType get_type() const override {
        return AmsType::MULTIACE;
    }

    /**
     * @brief How a given U1 head is fed.
     *
     * Mirrors multiACE's own three-way split. UNKNOWN is the pre-first-frame
     * answer and must not be confused with FEEDER — before `ace` has been seen
     * we do not yet know, and guessing FEEDER would dispatch the native path to
     * a head the ACE actually owns.
     */
    enum class HeadSource { UNKNOWN, FEEDER, ACE, MANUAL };

    /// Per-unit topology. The base falls back to the SYSTEM-wide answer, which
    /// is PARALLEL here (the U1's four heads) — so without this override an ACE
    /// unit in head mode drew as a parallel fan instead of the combiner it
    /// physically is. Note `compute_system_tool_layout()` prefers the backend's
    /// answer over `AmsUnit::topology`, so populating the struct alone is not
    /// enough; it has to be answered here too.
    [[nodiscard]] PathTopology get_unit_topology(int unit_index) const override;

    /// An ACE-fed head's spool is described by the ACE, not by the U1 — see the
    /// base declaration. Returns the ACE's global unit index for such a head,
    /// nullopt for a feeder head or any ACE bay (those describe themselves).
    [[nodiscard]] std::optional<int> slot_identity_owner_unit(int slot_index) const override;

    /// The ACE bay behind an ACE-fed head, as a global slot index. Pairs with
    /// slot_identity_owner_unit() so the head and its bay share one spool
    /// number instead of each consuming one.
    [[nodiscard]] std::optional<int> slot_identity_owner_slot(int slot_index) const override;

    /// How head @p head is fed, per the last `ace` frame.
    [[nodiscard]] HeadSource head_source_kind(int head) const;

    /// See AmsBackendSnapmaker::preload_finish_ends_unload(). True for an
    /// ACE-fed head: `ACE_UNLOAD_HEAD` hands the retract to the ACE, so the
    /// U1's channel_state stops at preload_finish and `unload_finish` never
    /// arrives. A feeder or manual head still runs the native sequence and
    /// keeps the stock answer.
    [[nodiscard]] bool preload_finish_ends_unload(int head) const override {
        // Reads head_kind_ DIRECTLY. The caller is the channel_state parse,
        // which already holds mutex_, and head_source_kind() takes it again --
        // a non-recursive std::mutex, so that self-deadlocked the main thread
        // the first time an ACE-fed head reached preload_finish, i.e. on the
        // first real unload. The whole UI froze.
        return head >= 0 && head < NUM_TOOLS && head_kind_[head] == HeadSource::ACE;
    }

    /// See AmsBackend::change_tool_completes_load(). True for the U1's own four
    /// heads, whose filament sits at the head already -- `T{n}` really is the
    /// whole load there. False for every ACE bay: mounting the head the bay
    /// feeds moves the carriage and feeds nothing, and the bay has to be named
    /// (`ACE_LOAD_HEAD HEAD=h ACE=a SLOT=s`).
    [[nodiscard]] bool change_tool_completes_load(int slot_index) const override {
        return slot_index < NUM_TOOLS;
    }

    /// An ACE bay, split out of its GLOBAL slot index.
    ///
    /// Global layout: slots 0..3 are the U1's heads (unit 0), then each ACE
    /// contributes ACE_SLOTS_PER_UNIT bays in unit order. `head` is the head
    /// that ACE feeds in head mode, or -1 when no head is bound to it.
    struct BaySource {
        int ace_index = -1; ///< multiACE's own 0-based device index
        int bay = -1;       ///< Bay within that ACE
        int head = -1;      ///< Head this ACE feeds, or -1
    };
    /// nullopt when @p slot_index is a head rather than a bay, or out of range.
    [[nodiscard]] std::optional<BaySource> bay_source(int slot_index) const;

    /// The U1's own step model, with the unload's last step naming its
    /// destination when the head being unloaded is ACE-fed.
    ///
    /// The filament does not just leave the nozzle, it travels back into the
    /// ACE, and "Retract" alone gave no sign of that -- the step sits there for
    /// the length of the whole retract, which is most of the operation.
    ///
    /// A RENAME, not an added step: the firmware drives the step index (phase 3
    /// for unload_doing), so a fifth step would have nothing to advance it and
    /// would sit Pending forever.
    [[nodiscard]] OperationStepModel get_operation_step_model(StepOperationType op) const override;

    /// A BAY is actively loaded when it is the one currently feeding a head.
    ///
    /// The base rule is `slot_index == get_current_slot()`, and the current slot
    /// is a HEAD (3), so no bay ever matched and a loaded bay drew exactly like
    /// its idle neighbours. Answered only for bays: the U1's own heads keep the
    /// inherited rule, which is what the SnapSwap page has always shown.
    /// Deliberately NOT done by claiming has_per_slot_loaded_authority(), which
    /// would switch the heads over to a per-slot latch as a side effect.
    [[nodiscard]] bool slot_is_actively_loaded(int slot_index) const override;

    // Every ACE reports temperature and humidity. Answered at the BACKEND level,
    // as the interface asks it: unit 0 is the U1 itself and has no sensor, but
    // the per-unit `ams_env_ind_<n>_visible` subject already hides the indicator
    // there. Inherited from AmsBackendSnapmaker this was false, which hard-hid
    // the indicator on the unit detail page -- and with it the only route to the
    // dryer and auto-dry controls for a drilled-into ACE.
    [[nodiscard]] bool has_environment_sensors() const override {
        return true;
    }

    // Dryer. multiACE exposes parameterised commands -- ACE_DRY ACE=n [TEMP=]
    // [DURATION=] and ACE_STOP_DRYING [ACE=n] -- so temperature and duration are
    // set live, with no config edit and no Klipper restart. (The ACED__DRY_START_n
    // macros in multiACE's macro table are the parameterless Fluidd buttons and
    // are NOT what this uses.)
    [[nodiscard]] DryerInfo get_dryer_info(int unit = 0) const override;
    AmsError start_drying(float temp_c, int duration_min, int fan_pct = -1, int unit = 0) override;
    AmsError stop_drying(int unit = 0) override;

    // Humidity-controlled drying. `ACE_SET_AUTO_DRY ACE=n ...` takes each field
    // independently, so arming the rule never restates thresholds the user set
    // elsewhere. Its full surface, read off multiACE's own web UI rather than
    // guessed (see the plan's § "Auto-dry"):
    //   ACE=n [ENABLE=0|1] [TEMP=35..80] [RH_START=5..95] [RH_END=1..94]
    //         [MASTER=<ace idx|-1>] [ADD_TIME=0..600]
    [[nodiscard]] AutoDryInfo get_auto_dry_info(int unit = 0) const override;
    AmsError set_auto_dry_enabled(bool enabled, int unit = 0) override;

    /// Which (unit, slot) is seated at head @p head, or nullopt when the head is
    /// not ACE-fed. `unit_index` is the GLOBAL unit index (ace_index + 1), since
    /// unit 0 is the U1 itself.
    struct SeatedSource {
        int unit_index = -1; ///< Global AmsUnit index (ace_index + 1)
        int ace_index = -1;  ///< multiACE's own 0-based device index
        int slot = -1;       ///< Slot within that ACE
    };
    [[nodiscard]] std::optional<SeatedSource> seated_source(int head) const;

  protected:
    void handle_status_update(const nlohmann::json& notification) override;
    const char* backend_log_tag() const override {
        return "[AMS multiACE]";
    }

    // ACE-fed heads take the ACE command path; feeder heads fall through to the
    // inherited native U1 path. See the class comment.
    AmsError do_load_filament(int slot_index) override;
    AmsError do_unload_filament(int slot_index) override;

  private:
    /// Parse the `ace` object. Caller must hold mutex_.
    void parse_ace_object_locked(const nlohmann::json& ace, bool& changed);
    /// Resolve each bay's spool through `spool_binding` -> `spools`. Caller must
    /// hold mutex_. See AceUnitState::BaySpool for why this is not `slots[]`.
    void parse_spool_table_locked(const nlohmann::json& ace, bool& changed);

    /// Per-bay identity that multiACE keeps in a FILE, not in its Klipper object.
    ///
    /// `slot_overrides.json` is what multiACE's own web UI resolves from — every
    /// bay it reports comes back `source: "override"` — and it covers bays the
    /// spool table does not: a bay can carry a material and colour with no spool
    /// bound to it at all, which read as empty here until this was added.
    ///
    /// Fetched over Moonraker's file API rather than multiACE's optional FastAPI
    /// service, so it needs nothing installed beyond multiACE itself.
    struct BayOverride {
        bool set = false;
        std::string material;
        std::string brand;
        std::optional<uint32_t> color_rgb;
    };
    using OverrideMap = std::array<std::array<BayOverride, ACE_SLOTS_PER_UNIT>, MAX_ACE_UNITS>;

    /// Pure, and static so it can run on the HTTP thread without touching `this`.
    [[nodiscard]] static OverrideMap parse_slot_overrides(const std::string& content);
    /// Ask Moonraker for the file. Main thread; applies its result via the
    /// lifetime token.
    void fetch_slot_overrides();
    /// Rebuild units 1..N from the parsed ACE inventory. Caller must hold mutex_.
    void rebuild_ace_units_locked();

    /// multiACE's `mode`: true when one ACE binds to one head (all its slots
    /// feed that head) rather than slot *s* feeding head *s*.
    bool head_mode_ = true;
    int device_count_ = 0;

    std::array<HeadSource, NUM_TOOLS> head_kind_{
        {HeadSource::UNKNOWN, HeadSource::UNKNOWN, HeadSource::UNKNOWN, HeadSource::UNKNOWN}};
    std::array<std::optional<SeatedSource>, NUM_TOOLS> head_seated_{};
    /// Which ACE feeds each head, off `head_ace`. Meaningful ONLY once
    /// head_kind_ says the head is ACE-fed — head_ace names an ACE for every
    /// head, including ones on their stock feeder. Unlike head_seated_ this
    /// survives an empty head, so it is what the wiring questions ask.
    std::array<int, NUM_TOOLS> head_ace_index_{{-1, -1, -1, -1}};

    /// Per-ACE inventory as last parsed, indexed by multiACE's ace_index.
    struct AceUnitState {
        bool connected = false;
        /// Wire protocol generation ("v1" / "v2") — the only field that says
        /// which ACE model this is. See ace_model_name().
        std::string protocol;
        int temp = 0;
        int humidity = 0;
        /// Live dryer state, straight off `aces[].dryer_status`.
        bool drying = false;
        int dryer_target_c = 0;
        int dryer_duration_min = 0;
        int dryer_remaining_min = 0;
        /// Humidity-controlled drying, off `aces[].auto_dry` and its sibling
        /// `auto_dry_running`. `has_auto_dry` is what gates the UI: a unit that
        /// has never reported the block has no auto-dry at all, which is a
        /// different thing from one reporting it disabled.
        bool has_auto_dry = false;
        bool auto_dry_enabled = false;
        bool auto_dry_running = false;
        float auto_dry_rh_start = 0.0f;
        float auto_dry_rh_end = 0.0f;
        int auto_dry_temp_c = 0;
        /// multiACE's own ACE index of the followed unit, or -1 for none.
        int auto_dry_master = -1;
        int auto_dry_add_time_min = 0;
        std::array<bool, ACE_SLOTS_PER_UNIT> gate_present{{false, false, false, false}};
        std::array<std::string, ACE_SLOTS_PER_UNIT> material{};
        /// What the user entered in multiACE, resolved from `spools` through
        /// `spool_binding`. This — not `slots[].material` — is where a bay's
        /// identity actually lives: the inline fields are only filled from RFID
        /// and read empty on every non-RFID spool.
        struct BaySpool {
            bool bound = false;
            std::string material;
            std::string vendor;
            std::string label;
            std::string sku;
            int spoolman_id = 0;
            std::optional<uint32_t> color_rgb;
            // No weight here on purpose — Spoolman owns remaining AND total, and
            // multiACE's `weight_g` is only a copy of it. See where this is applied.
        };
        std::array<BaySpool, ACE_SLOTS_PER_UNIT> spool{};
        using SpoolEntry = BaySpool;
        /// nullopt = the ACE reported no colour, so keep SlotInfo's default
        /// rather than painting the slot black.
        std::array<std::optional<uint32_t>, ACE_SLOTS_PER_UNIT> color_rgb{};
    };
    std::array<AceUnitState, MAX_ACE_UNITS> ace_units_{};

    /// multiACE's spool table, keyed by spool id, and the bay->id bindings.
    /// Cached SEPARATELY because the two arrive independently: a frame that
    /// moves a spool between bays carries `spool_binding` and no `spools`, so
    /// resolving them together lost every detail on the next binding change.
    std::map<std::string, AceUnitState::BaySpool> spool_table_;
    std::array<std::array<std::string, ACE_SLOTS_PER_UNIT>, MAX_ACE_UNITS> bay_spool_id_{};

    /// Last fetched contents of slot_overrides.json, and the `ace.event_seq`
    /// that fetch was for. The file is not on the WebSocket, so event_seq — which
    /// multiACE bumps on its own state changes — is what says it may have moved.
    OverrideMap slot_overrides_{};
    int64_t overrides_fetched_seq_ = -1;
    bool override_fetch_in_flight_ = false;
    /// Set under mutex_ when a frame's event_seq outruns overrides_fetched_seq_;
    /// read and cleared by handle_status_update AFTER the lock is dropped, since
    /// the fetch must not be issued while holding it.
    bool override_refetch_wanted_ = false;
};
