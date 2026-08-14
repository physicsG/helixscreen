// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#if HELIX_HAS_CFS

#include "ams_subscription_backend.h"
#include "async_lifetime_guard.h"
#include "filament_slot_override.h"
#include "filament_slot_override_store.h"

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

class CfsTestAccess;

namespace helix::printer {

/// Static CFS utility functions (code stripping, color parsing, TNN
/// addressing). The material table this class used to own was retired in
/// favor of FilamentCatalog::load_codes("cfs") — see AmsBackendCfs::parse_box_status.
class CfsMaterialDb {
  public:
    /// Strip CFS material_type code prefix: "101001" -> "01001", "-1" -> ""
    static std::string strip_code(const std::string& code);

    /// Parse CFS color: "0RRGGBB" -> 0xRRGGBB, sentinels -> 0x808080
    static uint32_t parse_color(const std::string& color_str);

    /// Global slot index -> TNN name: 0 -> "T1A", 4 -> "T2A"
    static std::string slot_to_tnn(int global_index);

    /// TNN name -> global slot index: "T1A" -> 0, "T2A" -> 4, invalid -> -1
    static int tnn_to_slot(const std::string& tnn);

    /// Default color for unknown/sentinel slots
    static constexpr uint32_t DEFAULT_COLOR = 0x808080;
};

/// Decode CFS key8xx error codes into human-readable AmsAlerts
class CfsErrorDecoder {
  public:
    /// Decode a CFS error code. Returns nullopt for unknown codes.
    static std::optional<AmsAlert> decode(const std::string& key_code, int unit_index,
                                          int slot_index);

    /// Look up just the message+hint for a code, without slot/unit context.
    /// Used by the global gcode-error toast handler to translate raw Klipper
    /// `!! {"code":"key***","msg":"..."}` lines into friendly text.
    /// Returns {message, hint} or nullopt for unknown codes.
    static std::optional<std::pair<const char*, const char*>>
    lookup_message(const std::string& key_code);

    /// Variant that splices the `values` array (e.g. `[1,"B"]` from
    /// `!! {"code":"key849","values":[1,"B"]}`) into the user-facing
    /// message when the code's value-format is known. Returns full
    /// `std::string` so the caller doesn't have to mix const-char + string
    /// concatenation. Falls back to the un-augmented message+hint when
    /// the values shape is unknown for that code.
    static std::optional<std::pair<std::string, std::string>>
    lookup_message_with_values(const std::string& key_code, const nlohmann::json& values);
};

/// Macro dialect emitted by the CFS backend.
///
/// K2 stock firmware exposes the CR_BOX_* primitives (CR_BOX_PRE_OPT,
/// CR_BOX_EXTRUDE, CR_BOX_WASTE, CR_BOX_FLUSH, CR_BOX_END_OPT, CR_BOX_CUT,
/// CR_BOX_RETRUDE) plus the BOX_* envelope (BOX_SAVE_FAN, BOX_MODE_WAIT,
/// BOX_GO_TO_EXTRUDE_POS, BOX_NOZZLE_CLEAN, BOX_MOVE_TO_SAFE_POS,
/// BOX_RESTORE_FAN). Selected when the printer is detected as a non-K1
/// Creality with a `box` Klipper object.
///
/// K1 official CFS upgrade firmware (≥ v2.3.5.33) exposes a different,
/// non-prefixed set: BOX_EXTRUDE_MATERIAL, BOX_MATERIAL_FLUSH,
/// BOX_NOZZLE_CLEAN, BOX_CUT_MATERIAL, BOX_RETRUDE_MATERIAL,
/// BOX_GO_TO_EXTRUDE_POS, BOX_MOVE_TO_SAFE_POS. The K2-only fan-save and
/// mode-wait helpers are absent. Selected when PrinterDetector reports a
/// K1-series printer. Issue #968.
///
/// Fork — community Kalico ports of the K2 whose reimplemented `box` module
/// replaces Creality's closed one. `T<n>` and `BOX_UNLOAD` are high-level and
/// self-contained: box.py owns the whole feed/purge/park sequence, so
/// HelixScreen sends no stock envelope. Detected by `api_version` in the box
/// payload. See docs/devel/printers/CREALITY_K2_SUPPORT.md §
/// "Community Kalico port".
enum class CfsMacroVariant {
    K2,
    K1,
    Fork,
};

/// Shape of the `box` Moonraker object, which varies INDEPENDENTLY of the macro
/// dialect above.
///
/// Stock — Creality's own module (K1 and K2 both): per-unit `T1`..`T4` objects,
/// each holding four parallel arrays (`color_value`, `material_type`, `vender`,
/// `remain_len`), plus top-level `filament` / `map` / `same_material` /
/// `auto_refill`. Material codes need the CfsMaterialDb/FilamentCatalog decode.
///
/// Flat — community Kalico ports carrying a reimplemented box.py: a single
/// `slots[]` array of self-describing objects, plus `loaded_slot`,
/// `slot_filament_mask`, `load_path`, `materials`, `temp_c`, `humidity_pct`.
/// Zero key overlap with Stock. Materials and colors are already resolved, so
/// no code table is involved.
///
/// Detected from the PAYLOAD, never from PrinterDetector: the affected printers
/// report as stock K2 Plus hardware by every model signal, so the firmware swap
/// is invisible to model detection. Bundle QJKZEMTS.
enum class CfsSchema {
    Stock,
    Flat,
};

/// CFS (Creality Filament System) backend — K1 + K2 series printers with RS-485 CFS units
class AmsBackendCfs : public AmsSubscriptionBackend {
  public:
    AmsBackendCfs(IMoonrakerAPI* api, helix::IMoonrakerClient* client);

    /**
     * @brief Bare filament-sensor name CFS owns: "filament_sensor".
     *
     * K2 CFS exposes one filament_switch_sensor at the toolhead with the bare
     * name "filament_sensor". This name is conventional elsewhere, so it is
     * only claimed when CFS is the detected backend. Static and discovery-free;
     * @p discovery is accepted for signature uniformity. See
     * AmsBackend::sensor_belongs_to_backend (#1054).
     */
    static bool owns_filament_sensor(const std::string& bare_name,
                                     const helix::PrinterDiscovery& discovery);

    [[nodiscard]] AmsType get_type() const override {
        return AmsType::CFS;
    }

    // State queries
    [[nodiscard]] AmsSystemInfo get_system_info() const override;
    [[nodiscard]] SlotInfo get_slot_info(int slot_index) const override;

    // Path visualization
    [[nodiscard]] PathTopology get_topology() const override {
        return PathTopology::HUB;
    }
    [[nodiscard]] PathSegment get_filament_segment() const override;
    [[nodiscard]] PathSegment get_slot_filament_segment(int slot_index) const override;
    [[nodiscard]] PathSegment infer_error_segment() const override;

    /// handle_status_update() stamps SlotStatus::LOADED on the seated bay —
    /// the lane a unit names in T{n}.filament, once the toolhead switch says
    /// filament actually arrived — so the per-slot status carries the answer
    /// the aggregate pair used to hold alone. Before that stamp existed the
    /// parse wrote only AVAILABLE/EMPTY, which left the inherited
    /// can_unload_from_toolhead() false on every CFS slot (#1199).
    [[nodiscard]] bool has_per_slot_loaded_authority() const override {
        return true;
    }

  protected:
    // Operations. Gated by AmsSubscriptionBackend's NVI wrapper.
    // select_slot_moves_toolhead() stays false: CFS has no select at all
    // (do_select_slot returns not_supported — it loads directly).
    AmsError do_load_filament(int slot_index) override;
    AmsError do_unload_filament(int slot_index) override;
    AmsError do_select_slot(int slot_index) override;
    AmsError do_change_tool(int tool_number) override;

  public:
    AmsError reset() override;
    AmsError recover() override;
    AmsError cancel() override;

    // Load-vs-swap decision. K1 official CFS upgrade firmware reports a
    // *preloaded* (cassette-staged) slot via current_slot with the nozzle still
    // empty, so on K1 only filament_loaded implies a cut-before-load is needed.
    // K2 keeps the base behavior (filament_loaded OR current_slot >= 0). (#968)
    //
    // Every CFS bay merges into one extruder, so the base class's per-lane
    // independence arm can never fire here — deferring to it on K2 keeps that
    // path in one place without changing the answer.
    [[nodiscard]] bool needs_unload_before_load(const AmsSystemInfo& info,
                                                int target_slot) const override {
        if (macro_variant_ != CfsMacroVariant::K1) {
            return AmsBackend::needs_unload_before_load(info, target_slot);
        }
        return info.filament_loaded;
    }

    // Slot management (user overrides persisted via shared FilamentSlotOverrideStore)
    AmsError set_slot_info(int slot_index, const SlotInfo& info, bool persist = true) override;
    AmsError set_tool_mapping(int tool_number, int slot_index) override;

    // Explicit user-initiated override clear (e.g. "Clear slot metadata" button
    // in the AMS edit modal). Erases overrides_[slot_index], resets the
    // override-exclusive fields on the live SlotInfo, and fires
    // override_store_->clear_async. CFS firmware populates brand / color_name /
    // total_weight_g from its RFID material database, so those fields are
    // preserved. Only spool_name / spoolman_* / remaining_weight_g are zeroed.
    void clear_slot_override(int slot_index) override;

    // Explicit Clear Spool action. Fork firmware owns the persisted profile;
    // stock CFS dialects have no equivalent command.
    void clear_box_slot_profile(int slot_index);

    // Bypass (not supported)
    AmsError enable_bypass() override;
    AmsError disable_bypass() override;
    [[nodiscard]] bool is_bypass_active() const override {
        return false;
    }

    // Capabilities

    /**
     * @brief CFS auto-refill: available, firmware-managed, no per-slot relation.
     *
     * The box picks the refill spool itself from its own `same_material` groups
     * and exposes no per-slot mapping, so this backend deliberately does NOT
     * override get_endless_spool_config() - the base's empty relation is the
     * truthful answer, and it is what keeps the UI from drawing a backup
     * dropdown that could only ever read "None".
     *
     * `enabled` comes from `box.auto_refill` (stock) / `box.runout_swap_enabled`
     * (flat fork) via AmsSystemInfo::endless_spool_enabled, so on and off are now
     * distinguishable; the old struct hardcoded `supported = true` and buried the
     * real state in an untranslated `description` string.
     *
     * @note Takes `mutex_`; callers must NOT hold it.
     */
    [[nodiscard]] helix::printer::EndlessSpoolCapabilities
    get_endless_spool_capabilities() const override;
    [[nodiscard]] helix::printer::ToolMappingCapabilities
    get_tool_mapping_capabilities() const override;
    [[nodiscard]] std::vector<int> get_tool_mapping() const override;

    /// True except on K1, where BOX_MODIFY_TN no-ops (#968) so no confirming
    /// box frame ever arrives. See the definition for the full rationale.
    [[nodiscard]] bool reports_firmware_tool_mapping() const override;

    [[nodiscard]] uint64_t firmware_tool_mapping_generation() const override;
    [[nodiscard]] bool supports_auto_heat_on_load() const override {
        return true;
    }
    [[nodiscard]] bool has_environment_sensors() const override {
        return true;
    }
    [[nodiscard]] bool tracks_weight_locally() const override {
        return false;
    }
    [[nodiscard]] bool manages_active_spool() const override {
        return false;
    }
    [[nodiscard]] RemapStrategy get_remap_strategy() const override {
        return RemapStrategy::Native;
    }
    // CFS unloads filament from the toolhead at end-of-print and reloads it as
    // part of the next print-start sequence, so the toolhead is expected to be
    // empty at print-start. The runout sensor reading "no filament" then is by
    // design, not a fault — suppress the pre-print runout warning modal.
    [[nodiscard]] bool auto_unloads_after_print() const override {
        return true;
    }
    [[nodiscard]] std::vector<helix::printer::DeviceAction> get_device_actions() const override;
    AmsError execute_device_action(const std::string& action_id,
                                   const std::any& value = {}) override;

    // Static parsers (public for testing)

    /// Decide which `box` shape this payload is. Stock is the default for
    /// anything ambiguous — every shipped CFS is stock, and misrouting one to
    /// the flat parser would report zero slots.
    [[nodiscard]] static CfsSchema detect_schema(const nlohmann::json& box_json);

    /// Parse a `box` object, dispatching on detect_schema().
    static AmsSystemInfo parse_box_status(const nlohmann::json& box_json);

    /// Stock (`T1`..`T4`) parse. Split out of parse_box_status when the flat
    /// schema arrived; behavior unchanged.
    static AmsSystemInfo parse_stock_box_status(const nlohmann::json& box_json);

    /// Flat (`slots[]`) parse — community Kalico box.py reimplementations.
    static AmsSystemInfo parse_flat_box_status(const nlohmann::json& box_json);

    /// True when this `box` payload comes from the community box.py, i.e. the
    /// firmware speaks CfsMacroVariant::Fork.
    ///
    /// Requires `api_version == 1`, the explicit version for this command
    /// dialect. Do not infer commands from the `slots[]` status layout alone;
    /// another firmware may expose the same flat layout. The Fork commands are
    /// registered in Python, so PrinterDiscovery::has_macro() cannot see them.
    [[nodiscard]] static bool detect_fork_dialect(const nlohmann::json& box_json);

    /// `_BOX_SLOT_SET` — the Fork counterpart to the stock BOX_MODIFY_TN_DATA
    /// color write. Returns "" when the module would reject the command.
    ///
    /// SLOT, MATERIAL and COLOR are all required by box.py's cmd_slot_set, so
    /// unlike the stock path this cannot be a color-only write; the caller must
    /// supply the slot's material. Material is uppercased here to match the
    /// module's own `str(material).strip().upper()` normalization, so a later
    /// status frame echoes back exactly what we sent.
    static std::string slot_set_gcode(int global_slot_index, const std::string& material,
                                      uint32_t color_rgb, const std::string& brand,
                                      const std::string& name, int spoolman_id);

    // GCode helpers (public for testing)
    static std::string load_gcode(int global_slot_index,
                                  CfsMacroVariant variant = CfsMacroVariant::K2);
    static std::string unload_gcode(CfsMacroVariant variant = CfsMacroVariant::K2);
    static std::string swap_gcode(int global_slot_index,
                                  CfsMacroVariant variant = CfsMacroVariant::K2);
    static std::string reset_gcode();
    static std::string recover_gcode();

    /// Recognize the CFS runout handler's give-up messages and turn them into a
    /// CRITICAL runout fault with recovery buttons.
    ///
    /// Unlike AFC's and Happy Hare's overrides this deliberately claims
    /// **non-`!!`** lines: the box announces that it will not swap spools with
    /// `respond_info()`, which reaches us as a `// `-prefixed response. `!!`
    /// lines are handed straight back so the generic classifier keeps owning
    /// every `key8xx` code (including key840's "Reset CFS" action) exactly as
    /// before — that separation is what stops a runout double-surfacing.
    ///
    /// See docs/devel/printers/CREALITY_K2_SUPPORT.md § "Runout and auto-refill"
    /// for the firmware sequence these strings come from.
    [[nodiscard]] std::optional<helix::ErrorEvent>
    classify_error(const std::string& raw_line, const helix::ClassifyContext& ctx) const override;

  protected:
    /// Recovery buttons for a CFS runout. **Caller must hold mutex_** (base
    /// contract; this override takes no lock of its own and mutex_ is not
    /// recursive).
    [[nodiscard]] std::vector<helix::RecoveryAction> build_recovery_actions() const override;

    void handle_status_update(const nlohmann::json& notification) override;
    const char* backend_log_tag() const override {
        return "[AMS CFS]";
    }
    void on_started() override;

    /// Push the user's chosen color back to firmware via the undocumented
    /// `BOX_MODIFY_TN_DATA` gcode (registered by box_wrapper.cpython-39.so,
    /// not in `gcode/help`). Format reverse-engineered from K2's master-server
    /// binary: `BOX_MODIFY_TN_DATA ADDR=<1..4> NUM=<A|B|C|D> PART=color_value
    /// DATA=0RRGGBB`. Writes persist to `/mnt/UDISK/creality/userdata/box/tn_data.json`.
    ///
    /// Color-only for now — material_type uses CFS-internal codes (e.g. "101001"
    /// for PLA) that we lack a complete reverse-map for; round-tripping the
    /// material risks pushing a wrong/empty code and could confuse the K2's
    /// stock LCD or the LOAD_MATERIAL macros. Color is the primary user-edit
    /// anyway and round-trips cleanly because the format is just RGB hex.
    ///
    /// **CRITICAL:** sending invalid args (ADDR=0, malformed payload) triggers
    /// a `TypeError` deep in box_wrapper which Klipper escalates to
    /// `invoke_shutdown` — the entire printer goes offline and needs a full
    /// `RESTART`. This method validates `global_index` in [0, 16) and skips
    /// when `color_rgb == 0` BEFORE formatting the gcode. Non-fatal on dispatch
    /// failure (the override is in lane_data either way).
    ///
    /// Marked virtual + protected so test subclasses can override and capture
    /// the gcode without a live Moonraker connection.
    virtual void push_slot_color_to_firmware(int global_index, uint32_t color_rgb);

  private:
    friend class ::CfsTestAccess;

    std::string current_tnn_;
    bool motor_ready_ = true;

    // K1 vs K2 macro dialect, latched in ctor from PrinterDetector. Most
    // callers route through dispatch_action_script and pull the macro string
    // from the static helpers (load_gcode/unload_gcode/swap_gcode), so this
    // is read on the script-build side, not in hot paths.
    CfsMacroVariant macro_variant_ = CfsMacroVariant::K2;

    /// Monotonic count of box.map parses — firmware-sourced by construction,
    /// since the optimistic path writes system_info_ via assign_tool_slot()
    /// and never touches this (#1270).
    uint64_t firmware_map_generation_ = 0;

    /// Box schema last seen on the wire, latched by handle_status_update.
    ///
    /// Separate axis from macro_variant_ above: the dialect is latched once in
    /// the constructor from the printer model, but the schema cannot be — the
    /// affected printers report as stock K2 hardware, so it is only knowable
    /// from a payload. Stock until a payload says otherwise.
    ///
    /// Payload layout only. The command dialect is selected independently;
    /// Flat + Fork is supported, while an unidentified Flat implementation is
    /// kept off stock command paths.
    CfsSchema schema_ = CfsSchema::Stock;

    /// SUCCESS for stock schemas and the identified Fork dialect; returns
    /// not_supported for an unidentified Flat implementation.
    [[nodiscard]] AmsError reject_if_flat_schema(const char* operation) const;

    // Callback lifetime management
    helix::AsyncLifetimeGuard lifetime_;

    /// Dispatch a load/unload/swap CR_BOX_* script with proper completion
    /// semantics: ensures the toolhead is homed, sends the gcode, and flips
    /// `system_info_.action` back to IDLE *only when Klipper finishes the
    /// entire script* (success or error). The previous design relied on the
    /// `filament_switch_sensor` flipping to declare "done" — but that sensor
    /// triggers at the toolhead extruder, which is reached at the *end of
    /// CR_BOX_EXTRUDE* (step 2 of 5). The remaining `CR_BOX_WASTE` and
    /// `CR_BOX_FLUSH` (~3 min of nozzle-at-240 °C extrusion) ran while the
    /// UI told the user the load was idle.
    /// Marked virtual so test subclasses can capture the assembled load/swap/
    /// unload script (and the WITH/WITHOUT-material selection that produced it)
    /// without a live Moonraker connection. Private -- test access to call the
    /// real implementation directly goes through the ::CfsTestAccess friend
    /// shim (tests/test_helpers/cfs_test_access.h), not a `using` declaration.
    virtual AmsError dispatch_action_script(std::string gcode);

    /// Undo the derived LOADED stamp, putting back whatever the last parse
    /// wrote there. Caller must hold mutex_. Runs at the TOP of
    /// handle_status_update so check_hardware_event_clear, the lane_data mirror
    /// and apply_overrides all see firmware truth rather than a synthesized
    /// seat; restoring the saved status (rather than assuming AVAILABLE) is
    /// what keeps a bay firmware called EMPTY from acquiring a phantom spool
    /// when the toolhead clears. A no-op when the slot vector was rebuilt
    /// underneath the stamp, since the fresh parse already wrote truth there.
    void clear_seated_slot_stamp_locked();

    /// Re-derive the LOADED stamp from the aggregate pair and apply it. Caller
    /// must hold mutex_. CFS publishes the seated bay across two signals that
    /// arrive on separate frames — the per-unit T{n}.filament letter names the
    /// lane, the toolhead filament_switch_sensor says whether anything reached
    /// the nozzle — so this runs at the END of handle_status_update, after both
    /// branches have had their say, and again after the optimistic current_slot
    /// writes in load_filament()/change_tool().
    ///
    /// The stamp is applied even over an EMPTY bay: a spool pulled while still
    /// threaded leaves filament at the toolhead that the user must be able to
    /// unload, and refusing to stamp there would blank the active-lane
    /// highlight in exactly that case.
    void apply_seated_slot_stamp_locked();

    /// Global index the LOADED stamp currently sits on, and the status the
    /// parse had written there before it was overwritten. -1 / UNKNOWN when no
    /// stamp is outstanding.
    int seated_stamp_slot_ = -1;
    SlotStatus seated_stamp_prev_ = SlotStatus::UNKNOWN;

    /// Layer a configured FilamentSlotOverride for `slot_index` over `slot`,
    /// mutating `slot` in place. Override wins for every non-default field;
    /// default sentinels (empty strings, spoolman_id 0, weights -1, color_rgb 0)
    /// fall through to firmware-reported data. Callers must hold mutex_.
    /// Called from handle_status_update AFTER firmware parse populates the slot
    /// and AFTER check_hardware_event_clear, so the final SlotInfo visible via
    /// get_slot_info reflects the override layer.
    void apply_overrides(SlotInfo& slot, int slot_index);

    /// Hardware-event detection: CFS exposes per-slot RFID material data. The
    /// composite (material_type + color_value) raw RFID strings form a
    /// per-slot fingerprint. When the fingerprint changes between parses, the
    /// physical spool was swapped — clear the stored override so stale
    /// spool_name / spoolman_id / remaining_weight_g from the previous user
    /// don't bleed onto the new spool.
    ///
    /// Empty observed_uid (no tag / sentinel `-1` / `None`) is treated as
    /// "no signal" — never updates the baseline and never clears. First
    /// observation for a slot establishes the baseline and NEVER fires a
    /// clear. Must be called BEFORE apply_overrides so the clear's field
    /// reset isn't masked by a stale override layer.
    ///
    /// CFS-specific field policy on clear: CFS firmware populates
    /// brand/color_name/total_weight_g from its material database via RFID
    /// lookup, so those fields are NOT zeroed here — the parse has already
    /// written firmware-truth for the newly-inserted spool. Only strictly
    /// override-exclusive fields (spool_name / spoolman_id /
    /// spoolman_vendor_id / remaining_weight_g) are reset.
    ///
    /// One fingerprint component — color_value — is also WRITTEN by
    /// push_slot_color_to_firmware, so firmware eventually echoes our own edit
    /// back as a fingerprint change. That echo is not a swap. push_ therefore
    /// registers the expected post-write fingerprint with rfid_tracker_, which
    /// classifies the echo as OwnWriteEcho and leaves the override intact.
    ///
    /// Returns true iff the override was cleared, so the caller can skip the
    /// lane_data mirror for this parse (a DELETE and a POST against the same
    /// lane_data key in one pass is a write race — see handle_status_update).
    [[nodiscard]] bool check_hardware_event_clear(SlotInfo& slot, int slot_index,
                                                  const std::string& observed_uid);

    /// Clear a stale auto-mirrored override when firmware reports the bay
    /// EMPTY. CFS has no other ejection path: the RFID fingerprint LATCHES
    /// after a spool is pulled, so check_hardware_event_clear sees Unchanged
    /// forever and the lane_data record would keep advertising a spool that
    /// isn't there (stale color/material published to OrcaSlicer, plus
    /// apply_overrides promoting the empty bay back to AVAILABLE as a ghost
    /// slot).
    ///
    /// User-locked overrides are RETAINED across an empty bay — a deliberate
    /// assignment means "this is what lives in this slot", and a slot that is
    /// merely unloaded must not lose it. Only unlocked records, which by
    /// construction came from the firmware auto-mirror, are erased. Matches
    /// the AD5X IFS policy of retaining the lane->Spoolman override across
    /// empty (#1071).
    ///
    /// Caller must hold mutex_ and must call this BEFORE apply_overrides.
    /// Returns true iff the override was cleared.
    [[nodiscard]] bool clear_stale_override_on_removal_locked(SlotInfo& slot, int slot_index);

    // Shared helper used by every override-clear path (hardware event and
    // explicit user request). Caller must hold mutex_. Erases
    // overrides_[slot_index], resets strictly override-exclusive fields on
    // the provided SlotInfo (spool_name, spoolman_*, remaining_weight_g), and
    // fires clear_async. Brand / color_name / total_weight_g are preserved —
    // firmware populates them from the RFID material database.
    void clear_override_locked(int slot_index, SlotInfo& slot);

    // Persistent per-slot overrides. Writers (on_started bulk load,
    // set_slot_info persist path, check_hardware_event_clear) all hold
    // mutex_. Reads happen inside apply_overrides, which is also called
    // under mutex_.
    std::unique_ptr<helix::ams::FilamentSlotOverrideStore> override_store_;
    std::unordered_map<int, helix::ams::FilamentSlotOverride> overrides_;

    // Per-slot last-observed RFID fingerprint (material_type + "|" +
    // color_value, using the raw pre-strip_code strings), plus the pending
    // expected fingerprint for a color push we issued. Shared with the other
    // RFID-fingerprint backend (Snapmaker). All access under mutex_.
    helix::ams::SlotFingerprintTracker rfid_tracker_;

    // Sub-phase synthesis: CFS sets system_info_.action=LOADING/UNLOADING once
    // at gcode dispatch and leaves it there through cut/retract/feed/purge.
    // The step indicator therefore parks on the wrong sub-step (#Task #2).
    // We synthesize CUTTING / UNLOADING / LOADING / PURGING transitions from
    // physical signals — filament-sensor edges and extruder-target rises —
    // and overwrite system_info_.action so the UI's existing step mapping
    // shows the correct phase. All access under mutex_.
    struct PhaseTracker {
        bool active = false;                // true between dispatch and on_complete/on_error
        bool started_with_filament = false; // filament_detected at op start
        bool seen_filament_drop = false;    // true→false transition (cut completed)
        bool seen_filament_rise = false;  // false→true transition after a drop (new filament fed)
        bool reached_target_once = false; // current_temp ever within 5°C of target this op
        bool pending_purge_target = false; // target rose >10°C above baseline (waits for rise)
        bool seen_purge_signal = false;    // pending_purge_target gated by seen_filament_rise
        int baseline_target_deci = 0;      // extruder target when heating first completed
    };
    PhaseTracker phase_tracker_;
    int last_extruder_target_deci_ = 0;
    int last_extruder_temp_deci_ = 0;
    bool last_filament_detected_ = false;

    // Track box.filament_useup transitions. Read-only firmware flag (no BOX_*
    // setter). Decoded from a live runout->reload cycle on the K2 Plus
    // (2026-06-18): it is a runout / path-empty signal — 1 when no filament is
    // established at the box gate (pre-load and runout), 0 when loaded and
    // feeding. Coincides with the runout pause, clears on reload. Logged at
    // debug; not yet surfaced to the UI.
    int last_filament_useup_ = -1;

    // Capture op-start state (filament + extruder target). Sets phase_tracker_.active.
    // Caller must hold mutex_.
    void begin_phase_tracking();

    // Reset phase tracker on op completion. Caller must hold mutex_.
    void end_phase_tracking();

    // Drive phase machine on signal changes. Caller must hold mutex_.
    void on_filament_transition_locked(bool new_detected);
    void on_extruder_temp_change_locked(int new_temp_deci, int new_target_deci);

    // Recompute system_info_.action from phase_tracker_ state.
    // Caller must hold mutex_.
    void apply_synthesized_action_locked();
};

} // namespace helix::printer

#endif // HELIX_HAS_CFS
