// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ams_subscription_backend.h"
#include "filament_slot_override.h"
#include "filament_slot_override_store.h"

#include <array>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

class SnapmakerTestAccess;
class SnapmakerRealtimeTestAccess;
class RunoutScopeTestAccess;

/**
 * @file ams_backend_snapmaker.h
 * @brief Snapmaker U1 SnapSwap toolchanger backend
 *
 * The Snapmaker U1 is a 4-toolhead printer with custom Klipper extensions.
 * Each extruder has state fields (park_pin, active_pin, activating_move)
 * and RFID tags provide filament info per channel.
 *
 * Klipper Objects:
 * - extruder0..3 with custom fields: state, park_pin, active_pin,
 *   activating_move, extruder_offset, switch_count, retry_count, error_count
 * - filament_detect with info/state per channel and filament_feed left/right
 * - toolchanger, toolhead, print_task_config
 *
 * Path topology is PARALLEL (each tool has its own independent path).
 */

/// Per-extruder tool state from Snapmaker custom Klipper fields
struct ExtruderToolState {
    std::string state;                                ///< e.g., "PARKED", "ACTIVE", "ACTIVATING"
    bool park_pin = false;                            ///< Tool is in park position
    bool active_pin = false;                          ///< Tool is in active position
    bool activating_move = false;                     ///< Tool change move in progress
    std::array<float, 3> extruder_offset = {0, 0, 0}; ///< XYZ offset
    int switch_count = 0;                             ///< Total tool changes for this extruder
    int retry_count = 0;                              ///< Tool change retries
    int error_count = 0;                              ///< Tool change errors
};

/// RFID tag data parsed from filament_detect info
struct SnapmakerRfidInfo {
    std::string main_type;         ///< e.g., "PLA", "PETG"
    std::string sub_type;          ///< e.g., "SnapSpeed", "Basic"
    std::string manufacturer;      ///< e.g., "Polymaker"
    std::string vendor;            ///< e.g., "Snapmaker"
    uint32_t color_rgb = 0x808080; ///< RGB color (ARGB masked to 0x00FFFFFF)
    int hotend_min_temp = 0;
    int hotend_max_temp = 0;
    int bed_temp = 0;
    int weight_g = 0; ///< Spool weight in grams
    /// Canonical string form of CARD_UID (e.g. "144,32,196,2"). Empty when no
    /// tag is present, the RFID reader is disabled, or the field is missing.
    /// Used by the override system as the hardware-event signal: a change
    /// means the physical spool was swapped.
    std::string uid;
};

class AmsBackendSnapmaker : public AmsSubscriptionBackend {
  public:
    AmsBackendSnapmaker(IMoonrakerAPI* api, helix::IMoonrakerClient* client);

    ~AmsBackendSnapmaker() override;

    /// The U1's four heads are independent toolheads, each drawn and labelled
    /// below its slot — so a "T3" badge on the spool repeats what the toolhead
    /// already says, and did it twice on an ACE-fed head whose spool is a range.
    /// Same reasoning AmsBackendToolChanger gives; the U1 simply never opted in.
    [[nodiscard]] bool should_hide_slot_tool_badge() const override {
        return true;
    }

    /// `print_task_config` carries filament_type and filament_vendor per head,
    /// so the printer states what is loaded even though it stores no Spoolman
    /// id (has_firmware_spool_persistence() is separately false). Inherited by
    /// AmsBackendMultiAce, where the ACE's own table covers its bays.
    [[nodiscard]] bool has_firmware_filament_identity() const override {
        return true;
    }

    [[nodiscard]] AmsType get_type() const override {
        return AmsType::SNAPMAKER;
    }

    // State queries
    [[nodiscard]] AmsSystemInfo get_system_info() const override;
    [[nodiscard]] SlotInfo get_slot_info(int slot_index) const override;

    // Operation step bar. The U1 firmware reports a granular channel_state that
    // classify_channel_state maps to a per-direction step index published via the
    // ams_operation_phase subject, so the step model and its driving index live in
    // the backend (the sidebar renders generically). LOAD is a 5-step model
    // (Home -> Select -> Heat -> Feed filament -> Purge); UNLOAD is 4 steps
    // (Home -> Select -> Heat -> Retract). The Heat step shows a live nozzle temp.
    [[nodiscard]] OperationStepModel get_operation_step_model(StepOperationType op) const override;
    [[nodiscard]] lv_subject_t* get_operation_step_index_subject(StepOperationType op) override;

    /// Snapmaker U1 has 4 independent extruders (extruder, extruder1, extruder2,
    /// extruder3), one per tool. Tool N sources slot N directly — identity mapping.
    [[nodiscard]] std::optional<int> slot_for_extruder(int extruder_idx) const override {
        if (extruder_idx < 0 || extruder_idx >= static_cast<int>(get_system_info().total_slots)) {
            return std::nullopt;
        }
        return extruder_idx;
    }

    // Path visualization (PARALLEL topology — each tool is independent)
    [[nodiscard]] PathTopology get_topology() const override {
        return PathTopology::PARALLEL;
    }

    // needs_unload_before_load() is answered by the base class: every lane here
    // is PARALLEL, so slot_has_independent_path() is true for all of them and the
    // serial rule never applies. See AmsBackend for why, including the `T{n}`
    // load this backend used to dispatch.
    [[nodiscard]] PathSegment get_filament_segment() const override;
    [[nodiscard]] PathSegment get_slot_filament_segment(int slot_index) const override;
    [[nodiscard]] PathSegment infer_error_segment() const override;

    // Per-slot "filament is loaded to THIS tool's toolhead". Returns the
    // channel_state latch (loaded_at_toolhead_), driven by filament_feed
    // channel_state transitions — true between load_finish and the next
    // unload_finish/wait_insert/preload_finish. The per-tool motion sensor
    // (e{N}_filament) is NOT used here: on current firmware it fails to drop to
    // false after an unload, so it can't answer "is this lane loaded".
    [[nodiscard]] bool slot_has_filament_at_toolhead(int slot_index) const override;

    // Per-tool LOADED status. The U1 has 4 independent toolheads, so each slot's
    // loaded state is tracked separately rather than derived from a single
    // current_slot. Drives the active-lane highlight per tool.
    [[nodiscard]] bool slot_is_actively_loaded(int slot_index) const override;

  protected:
    // Operations. Every one of these drives the toolhead: AUTO_FEEDING forwards
    // to FEED_AUTO, which homes before it feeds, and `T{n}` moves the carriage.
    // AmsSubscriptionBackend's NVI wrapper refuses them while a print owns the
    // toolhead; PAUSED still passes (filament_ops_self_home() is false), which
    // is what keeps U1 runout recovery working (#991).
    AmsError do_load_filament(int slot_index) override;
    AmsError do_unload_filament(int slot_index) override;
    AmsError do_select_slot(int slot_index) override;
    AmsError do_change_tool(int tool_number) override;

    /// On the U1 a slot select IS a physical tool change — do_select_slot()
    /// forwards to do_change_tool(), which emits `T{n}` and moves the carriage.
    [[nodiscard]] bool select_slot_moves_toolhead() const override {
        return true;
    }

  public:
    // The base PARALLEL gate offers Unload for any tool with filament in its
    // buffer (is_present()). On the U1 that keeps offering Unload after a tool
    // is already unloaded — the firmware retracts the filament to the buffer
    // (channel_state preload_finish/unload_finish) but filament_exist stays
    // true, so the slot remains AVAILABLE. Override to additionally require the
    // channel_state load latch (loaded_at_toolhead_), which is true only while
    // filament is loaded at the toolhead (between load_finish and the next
    // unload_finish). The motion sensor was tried first but fails to clear after
    // an unload on current firmware; channel_state is the authoritative signal
    // (u1_channel_state_reference.md). Still offers Unload for every toolhead
    // physically loaded (active or parked), preserving the per-tool unload fix.
    [[nodiscard]] bool can_unload_from_toolhead(int slot_index) const override;

    // The U1 is a toolchanger: `PARK_EXTRUDER` returns the mounted head to its
    // dock and leaves the filament where it is. It is firmware-native and ships
    // with no description, so it does NOT appear in `gcode/help` — verified
    // instead by its use in the machine's own `print_end` macro, bare and
    // parameterless, right after the auto-unload.
    [[nodiscard]] bool supports_toolhead_park() const override {
        return true;
    }
    AmsError park_toolhead() override;

    // Recovery (not supported)
    AmsError recover() override;
    AmsError reset() override;
    AmsError cancel() override;

    // Resume preparation. Snapmaker latches a motion-sensor runout exception
    // in Klipper that RESUME alone can't clear when the printer's
    // extruders_used flags are all false. When sensor_filament_present_ is
    // false for the active tool, run a single gcode chain: disable the runout
    // sensor, heat to the slot's recorded nozzle min temp (fallback 200°C),
    // extrude 30 mm to push past the encoder, re-enable the sensor. Otherwise
    // invoke the callback immediately so the caller dispatches RESUME without
    // delay. on_ready is always called on the main thread.
    void prepare_for_resume(int slot_index, ResumeReadyCallback on_ready) override;

    // True when the motion sensor reports runout but the port sensor still
    // reads filament present — i.e. the encoder is stale (e.g., it never
    // saw the start-of-print purge) but physical filament is in the buffer.
    // Callers (FilamentRunoutHandler) auto-recover silently instead of
    // showing the modal.
    [[nodiscard]] bool is_stuck_motion_sensor_runout(int slot_index) const override;

    // Snapmaker U1's Resume runs AUTO_FEEDING (loads filament to the nozzle)
    // before RESUME, so Resume alone recovers a runout. The runout dialog uses
    // this to present Resume as primary and demote manual Load/Unload/Purge.
    [[nodiscard]] bool recovers_filament_on_resume() const override {
        return true;
    }

    // The U1 drives load/unload entirely on its own, so an idle lane going empty
    // (a hand-pull, or a lane left unloaded) needs no operator action and the
    // idle runout-guidance modal is just noise. Mid-print runout is a separate
    // path and is unaffected.
    [[nodiscard]] bool should_suppress_idle_runout_modal() const override {
        return true;
    }

    // Snapmaker U1 uses firmware-native print_task_config gcode
    // (SET_PRINT_USED_EXTRUDERS / SET_PRINT_EXTRUDER_MAP) emitted before
    // PRINT_START; no gcode-file rewrite is needed.
    [[nodiscard]] RemapStrategy get_remap_strategy() const override {
        return RemapStrategy::SnapmakerNative;
    }

    // Configuration
    AmsError set_slot_info(int slot_index, const SlotInfo& info, bool persist = true) override;
    AmsError set_tool_mapping(int tool_number, int slot_index) override;

    // Explicit user-initiated override clear (e.g. "Clear slot metadata" button
    // in the AMS edit modal). Erases overrides_[slot_index], resets the
    // override-exclusive fields (spool_name, spoolman_*, remaining_weight_g) on
    // the live SlotInfo, and fires override_store_->clear_async. Brand /
    // color_name / total_weight_g are preserved — on Snapmaker those fields
    // are populated from the RFID tag, so they reflect firmware truth.
    // The hardware-event detector calls this internally once a CARD_UID change
    // confirms a physical swap.
    void clear_slot_override(int slot_index) override;

    // Bypass (not applicable for tool changers)
    AmsError enable_bypass() override;
    AmsError disable_bypass() override;
    [[nodiscard]] bool is_bypass_active() const override {
        return false;
    }

    // Snapmaker U1's firmware errors if SET_PRINT_USED_EXTRUDERS /
    // SET_PRINT_EXTRUDER_MAP arrive mid-print, so the config must be sent before
    // PRINT_START. Always-on (even with no remap) to suppress a spurious-feed
    // runout — the slicer auto-feeds heads the print doesn't use → empty head →
    // runout cancel.
    [[nodiscard]] bool requires_preprint_send() const override {
        return true;
    }

    // Builds the firmware-native pre-print command sequence for print_task_config.
    // tools_used: logical tools the gcode body uses (ParsedGCodeFile::tools_used_indices).
    // remap:      logical tool -> physical head, ONLY for tools the user changed from identity.
    //             Tools absent from `remap` use default_head(t) = (t>=0 && t<=3) ? t : 0.
    // Returns newline-joined gcode (NO trailing newline), or "" when tools_used is empty.
    // Pure — no api_/network access, trivially unit-testable.
    [[nodiscard]] std::string build_preprint_gcode(const std::set<int>& tools_used,
                                                   const std::map<int, int>& remap) const override;

    // Static parsers (public for testing)
    /// Parse one extruder object into tool state.
    ///
    /// @param json  The (possibly PARTIAL) extruder object from a status frame.
    /// @param prev  State to start from. Moonraker sends deltas, and an extruder
    ///              emits `temperature` constantly, so most frames carry ONLY
    ///              that — with a default-constructed start those frames erased
    ///              state/park_pin/active_pin a fraction of a second after a
    ///              toolchange set them, and the mounted tool stopped being
    ///              elected. Seed with the previous state so absent keys are
    ///              preserved rather than reset.
    static ExtruderToolState parse_extruder_state(const nlohmann::json& json,
                                                  ExtruderToolState prev = {});
    static SnapmakerRfidInfo parse_rfid_info(const nlohmann::json& json);

  protected:
    void on_started() override;
    void handle_status_update(const nlohmann::json& notification) override;
    const char* backend_log_tag() const override {
        return "[AMS Snapmaker]";
    }

    /// The U1's fixed head count. Protected rather than private because
    /// AmsBackendMultiAce derives from this backend and indexes the same four
    /// heads; it is a compile-time constant of the hardware, not mutable state.
    static constexpr int NUM_TOOLS = 4;

    /// Validate slot index is within range. Protected for the same reason as
    /// NUM_TOOLS — AmsBackendMultiAce overrides do_load_filament/
    /// do_unload_filament and must apply the identical bounds check before
    /// deciding whether the ACE or the native path owns the head.
    AmsError validate_slot_index(int slot_index) const;

    /// Layer the user's configured override over a slot. Protected because
    /// AmsBackendMultiAce builds its ACE units AFTER this class's convergence
    /// point has run, so those slots would otherwise never see the override
    /// layer — a spool assigned to an ACE bay was written and then discarded.
    void apply_overrides_for(SlotInfo& slot, int slot_index) {
        apply_overrides(slot, slot_index);
    }

  private:
    friend class ::SnapmakerTestAccess;
    friend class ::SnapmakerRealtimeTestAccess;
    friend class ::RunoutScopeTestAccess;

    /// Per-extruder cached state
    std::array<ExtruderToolState, NUM_TOOLS> extruder_states_;

    /// Per-slot filament_motion_sensor "filament_detected" state. True means
    /// filament is currently being fed to the toolhead; false means runout has
    /// fired. Tracks `filament_motion_sensor e0..e3_filament` (and the
    /// matching `filament_switch_sensor` form as a fallback). Defaults to true
    /// so a slot without a configured sensor doesn't render as "runout" — the
    /// flag flips only when Klipper sends an explicit `filament_detected:false`
    /// for that sensor, which only happens on configured runout sensors. Read
    /// by get_filament_segment() / get_slot_filament_segment() to break the
    /// spool→toolhead line when the active tool has run out.
    std::array<bool, NUM_TOOLS> sensor_filament_present_{{true, true, true, true}};

    /// What `print_task_config.filament_type` last said for each head.
    ///
    /// Needed to tell a real material EDIT from a side effect of binding a
    /// Spoolman spool: apply_spool_to_slot() writes the spool's material into
    /// SlotInfo, and set_slot_info(persist=true) then stamped
    /// user_locked_material from "is the material non-empty", so simply linking
    /// a spool silently locked a material against the printer's own report —
    /// which apply_overrides then replayed forever. Compared against here so a
    /// bind that agrees with firmware locks nothing. Mirrors the AD5X IFS
    /// backend's last_firmware_color_ guard for the same class of self-inflicted
    /// lock (#965).
    std::array<std::string, NUM_TOOLS> last_firmware_material_{};

    /// Per-slot port/buffer sensor state — the filament_feed left/right
    /// .extruder{N}.filament_detected flag. Reads the physical-presence
    /// sensor at the spool/buffer side, NOT the encoder-based motion sensor.
    /// Used together with sensor_filament_present_ to differentiate real
    /// runouts (both false) from stale motion sensor false positives
    /// (motion=false, port=true). Defaults to false so a slot we've never
    /// seen filament_feed data for doesn't generate a "stuck sensor" false
    /// positive of its own — the auto-recover path requires the port sensor
    /// to explicitly report present.
    std::array<bool, NUM_TOOLS> port_sensor_filament_present_{{false, false, false, false}};

    /// Last value published to AmsState::set_active_tool_port_present for the
    /// active tool (#991). Tracks the active-tool port flag so we only push to
    /// the UI subject on an actual change. -1 = nothing published yet. Written
    /// only from handle_status_update (the single WS-thread writer).
    int last_published_port_present_ = -1;

    /// Per-slot "filament is loaded to THIS tool's toolhead" latch, driven
    /// purely from filament_feed.channel_state transitions (NOT the motion
    /// sensor). The per-tool motion sensor (e{N}_filament) does not reliably
    /// drop to false after an unload on current firmware — a freshly-unloaded
    /// lane still reads filament_detected=true (tip retracted from the melt
    /// zone only, filament parked past the toolhead sensor). channel_state is
    /// the authoritative load signal (verified live on a U1, firmware
    /// 20260608): load_finish means loaded, unload_finish / wait_insert /
    /// preload_finish mean not-loaded. Mirrors the firmware's own persisted
    /// config['load_finish'] flag. Set true on load_finish; cleared on
    /// unload_finish / wait_insert / preload_finish; left unchanged on every
    /// transient / in-progress / fail state. Defaults false: a lane we've never
    /// seen a channel_state for is treated as not-loaded. Read by
    /// slot_has_filament_at_toolhead() and can_unload_from_toolhead(). The
    /// motion sensor (sensor_filament_present_) still owns mid-print runout —
    /// a different question ("did the ACTIVE lane run out during extrusion").
    /// Set when the firmware POSITIVELY reported a retraction for this tool --
    /// the terminal `unload_finish` / `preload_finish` states, never the idle
    /// `wait_insert`. Cleared on load_finish and when the port sensor drops.
    ///
    /// Exists so the presence sensors can be believed by default while still
    /// suppressing the old-firmware quirk (captured on U1 20260608) where the
    /// motion sensor stayed true after an unload. On 20260722+ the sensors clear
    /// themselves, so this is normally inert.
    std::array<bool, NUM_TOOLS> retraction_seen_{{false, false, false, false}};

    /// "Filament is in this toolhead" -- the gate for Unload being offered and
    /// for a slot reading LOADED. Any of three signals, in descending strength:
    /// the witnessed-load latch; both presence sensors agreeing; or, for the
    /// MOUNTED tool only, a spool in its channel (filament_exist). The last two
    /// yield to a witnessed retraction. Caller must hold mutex_.
    [[nodiscard]] bool filament_present_at_tool_locked(int slot_index) const;

    std::array<bool, NUM_TOOLS> loaded_at_toolhead_{{false, false, false, false}};

    /// Layer a configured FilamentSlotOverride for `slot_index` over `slot`,
    /// mutating `slot` in place. Override wins for every non-default field.
    /// Callers must hold mutex_. Called from the tail of handle_status_update
    /// AFTER firmware data has been populated and BEFORE event emission, so
    /// the very next get_slot_info() reflects the overridden values.
    void apply_overrides(SlotInfo& slot, int slot_index);

    /// Hardware-event detection: if the RFID CARD_UID changes between parses,
    /// the user physically swapped the spool. Clears the stored override so
    /// stale brand / spool_name / spoolman_id from the previous user don't
    /// bleed onto the new spool. Empty observed_uid (no tag / unread) is
    /// treated as "no signal" — never updates the baseline and never clears.
    /// First observation for a slot establishes the baseline and NEVER fires
    /// a clear. Must be called BEFORE apply_overrides so the clear's field
    /// reset isn't masked by a stale override layer.
    ///
    /// Unlike the AD5X IFS implementation (which uses color as the event
    /// signal and needs a self-wipe guard in set_slot_info), Snapmaker uses
    /// the RFID UID — a hardware identifier the user cannot set via the UI.
    /// So set_slot_info registers no expected-echo value with rfid_tracker_;
    /// the baseline stays at whatever firmware last reported and user edits
    /// don't race. (CFS shares the tracker but DOES need that guard — it
    /// writes color_value back to the box, which is half of its fingerprint.)
    void check_hardware_event_clear(SlotInfo& slot, int slot_index,
                                    const std::string& observed_uid);

    // Shared helper used by every override-clear path (hardware event and
    // explicit user request). Caller must hold mutex_. Erases
    // overrides_[slot_index], resets strictly override-exclusive fields on
    // the provided SlotInfo (spool_name, spoolman_*, remaining_weight_g), and
    // fires clear_async. Brand / color_name / total_weight_g are preserved —
    // firmware populates them from the RFID tag.
    void clear_override_locked(int slot_index, SlotInfo& slot);

    // Persistent per-slot overrides. Writers (on_started bulk load,
    // set_slot_info persist path, check_hardware_event_clear) all hold mutex_.
    // Reads happen inside apply_overrides which is also under mutex_.
    std::unique_ptr<helix::ams::FilamentSlotOverrideStore> override_store_;
    std::unordered_map<int, helix::ams::FilamentSlotOverride> overrides_;

    // Per-slot last-observed RFID CARD_UID. Shared with the other
    // RFID-fingerprint backend (CFS). Snapmaker never calls expect() — nothing
    // here writes a UID back to firmware, so every change is external. All
    // access under mutex_.
    helix::ams::SlotFingerprintTracker rfid_tracker_;
};
