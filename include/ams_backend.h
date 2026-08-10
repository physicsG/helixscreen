// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file ams_backend.h
 * @brief Abstract platform-independent interface for multi-filament system operations
 *
 * @pattern Pure virtual interface + static create()/create_auto() factory methods
 * @threading Implementation-dependent; see concrete implementations
 *
 * @see ams_backend_happy_hare.cpp, ams_backend_afc.cpp
 */

#pragma once

#include "ams_error.h"
#include "ams_step_operation.h"
#include "ams_types.h"
#include "error_event.h"

class MoonrakerAPI;
namespace helix {
class MoonrakerClient;
class PrinterDiscovery;
} // namespace helix

// LVGL subject — forward-declared so backends can expose the subject that drives
// the operation step bar's current index without ams_state.h (circular include).
typedef struct _lv_subject_t lv_subject_t;

#include <any>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

/**
 * @brief Abstract interface for AMS/MMU backend implementations
 *
 * Provides a platform-agnostic API for multi-filament operations.
 * Concrete implementations handle system-specific details:
 * - AmsBackendHappyHare: Happy Hare MMU via Moonraker
 * - AmsBackendAfc: AFC-Klipper-Add-On via Moonraker
 * - AmsBackendMock: Simulator mode with fake data
 *
 * Design principles:
 * - Hide all backend-specific commands/protocols from AmsManager
 * - Provide async operations with event-based completion
 * - Thread-safe operations where needed
 * - Clean error handling with user-friendly messages
 */
class AmsBackend {
  public:
    virtual ~AmsBackend() = default;

    // ========================================================================
    // Event Types
    // ========================================================================

    /**
     * @brief Standard AMS event types
     *
     * Events are delivered asynchronously via registered callbacks.
     * Event names are strings to allow backend-specific extensions.
     */
    static constexpr const char* EVENT_STATE_CHANGED = "STATE_CHANGED"; ///< System state updated
    static constexpr const char* EVENT_SLOT_CHANGED = "SLOT_CHANGED";   ///< Slot info updated
    static constexpr const char* EVENT_LOAD_COMPLETE = "LOAD_COMPLETE"; ///< Load operation finished
    static constexpr const char* EVENT_UNLOAD_COMPLETE =
        "UNLOAD_COMPLETE";                                            ///< Unload operation finished
    static constexpr const char* EVENT_TOOL_CHANGED = "TOOL_CHANGED"; ///< Tool change completed
    static constexpr const char* EVENT_ERROR = "ERROR";               ///< Error occurred
    static constexpr const char* EVENT_ATTENTION_REQUIRED =
        "ATTENTION"; ///< User intervention needed

    // ========================================================================
    // Lifecycle Management
    // ========================================================================

    /**
     * @brief Initialize and start the AMS backend
     *
     * Connects to the underlying AMS system and starts monitoring state.
     * For real backends, this initiates Moonraker subscriptions.
     * For mock backend, this sets up simulated state.
     *
     * @return AmsError with detailed status information
     */
    virtual AmsError start() = 0;

    /**
     * @brief Stop the AMS backend
     *
     * Cleanly shuts down monitoring and releases resources.
     * Safe to call even if not started.
     */
    virtual void stop() = 0;

    /**
     * @brief Release subscriptions without unsubscribing
     *
     * Use during shutdown when the helix::MoonrakerClient may already be destroyed.
     * This abandons the subscription rather than trying to call into the client.
     * Backends that hold SubscriptionGuards should call release() on them.
     */
    virtual void release_subscriptions() {}

    /**
     * @brief Re-fetch authoritative slot/state from the printer.
     *
     * Called from UI sites where the user expects a fresh view of slot state
     * (e.g., entering the filament assignment screen). Lets users self-recover
     * from any drift between cached UI state and printer truth without a
     * full reconnect.
     *
     * Safe to call repeatedly — implementations should debounce/coalesce.
     * Default: no-op. Override in backends with a meaningful resync path.
     */
    virtual void request_resync() {}

    /**
     * @brief Check if backend is currently running/initialized
     * @return true if backend is active and ready for operations
     */
    [[nodiscard]] virtual bool is_running() const = 0;

    // ========================================================================
    // Event System
    // ========================================================================

    /**
     * @brief Callback type for AMS events
     *
     * @param event_name Event identifier (EVENT_* constants)
     * @param data Event-specific payload (JSON string or empty)
     */
    using EventCallback =
        std::function<void(const std::string& event_name, const std::string& data)>;

    /**
     * @brief Register callback for AMS events
     *
     * Events are delivered asynchronously and may arrive from background threads.
     * The callback should be thread-safe or post to main thread.
     *
     * @param callback Handler function for events
     */
    virtual void set_event_callback(EventCallback callback) = 0;

    // ========================================================================
    // State Queries
    // ========================================================================

    /**
     * @brief Get current AMS system information
     *
     * Returns a snapshot of the current system state including:
     * - System type and version
     * - Current tool/slot selection
     * - All unit and slot information
     * - Capability flags
     *
     * @return Current AmsSystemInfo (copy, safe for caller to hold)
     */
    [[nodiscard]] virtual AmsSystemInfo get_system_info() const = 0;

    /**
     * @brief Get the detected AMS type
     * @return AmsType enum value
     */
    [[nodiscard]] virtual AmsType get_type() const = 0;

    /**
     * @brief Whether this backend manages the Spoolman active spool itself
     *
     * Some backends (e.g., AFC) call spoolman_set_active_spool on tool
     * load/unload natively. When true, HelixScreen must NOT call
     * server.spoolman.post_spool_id to avoid racing with the backend.
     *
     * @return true if the backend manages active spool tracking
     */
    [[nodiscard]] virtual bool manages_active_spool() const {
        return false;
    }

    /**
     * @brief Whether this backend tracks filament weight locally
     *
     * Some backends (e.g., AFC, Happy Hare) track filament consumption via
     * extruder position and update slot weight in real time. When true,
     * HelixScreen must NOT overwrite slot weights from Spoolman polling,
     * because Spoolman's weight is stale (backends don't write back to it).
     *
     * @return true if the backend provides live weight tracking
     */
    [[nodiscard]] virtual bool tracks_weight_locally() const {
        return false;
    }

    /**
     * @brief Get information about a specific slot
     * @param slot_index Slot index (0 to total_slots-1)
     * @return SlotInfo struct (copy, safe for caller to hold)
     */
    [[nodiscard]] virtual SlotInfo get_slot_info(int slot_index) const = 0;

    /**
     * @brief Get current action/operation status
     * @return Current AmsAction enum value
     */
    [[nodiscard]] virtual AmsAction get_current_action() const = 0;

    /**
     * @brief Get currently selected tool number
     * @return Tool number (-1 if none, -2 for bypass on Happy Hare)
     */
    [[nodiscard]] virtual int get_current_tool() const = 0;

    /**
     * @brief Get currently selected slot number
     * @return Slot number (-1 if none, -2 for bypass on Happy Hare)
     */
    [[nodiscard]] virtual int get_current_slot() const = 0;

    /**
     * @brief Slot index currently sourced by the given extruder. Backends that model
     * per-extruder attribution (tool-changers with one spool per tool) override to
     * return the tool->slot mapping. Default returns nullopt; callers fall back to
     * aggregate filament_used_mm + current_slot().
     * @param extruder_idx 0-based extruder index (0 = primary, 1 = extruder1, ...)
     */
    [[nodiscard]] virtual std::optional<int> slot_for_extruder(int extruder_idx) const {
        (void)extruder_idx;
        return std::nullopt;
    }

    /**
     * @brief Channel A: backend-specific classification of one gcode-response
     *        line.
     *
     * Called from GcodeErrorRouter::process_line() (gcode_error_router.cpp),
     * before the generic error_classify::classify(), so domain-aware backends
     * can recognize their own error lines and attach accurate severity +
     * recovery actions. Return nullopt to defer to the generic classifier;
     * return an ErrorEvent to short-circuit it.
     *
     * @warning The router applies **no line filtering at all** — every response
     *          line reaches every backend, `!!` or not. Each override must gate
     *          itself. AFC and Happy Hare take only `!!` lines
     *          (helix::is_bang_line); CFS deliberately takes only NON-`!!`
     *          lines, because its give-up messages arrive via respond_info while
     *          its coded faults belong to the generic key8xx path.
     *
     * See docs/devel/FILAMENT_MANAGEMENT.md § "Two error channels" for the
     * per-backend gate and recovery table.
     *
     * @param raw_line  Unmodified gcode-response line to classify
     * @param ctx       Printer state at the time the line arrived
     * @return ErrorEvent if this backend claims the line, nullopt otherwise
     */
    [[nodiscard]] virtual std::optional<helix::ErrorEvent>
    classify_error(const std::string& /*raw_line*/, const helix::ClassifyContext& /*ctx*/) const {
        return std::nullopt;
    }

    /// Channel B: the current actionable fault, derived from backend STATUS
    /// rather than from a console line. Consulted only by AmsErrorBridge, and
    /// only on the rising edge into AmsAction::ERROR — a backend that never
    /// assigns that action can override this and still never be asked.
    ///
    /// Returns nullopt when there is no actionable error, or when a bespoke
    /// dialog owns the fault.
    ///
    /// The two channels are independent, not alternatives: AFC overrides BOTH
    /// (its `!!` lands before AFC pauses, so the line and the status edge each
    /// catch cases the other misses — #1171). Happy Hare and CFS are channel A
    /// only; AD5X IFS and QIDI are channel B only.
    [[nodiscard]] virtual std::optional<helix::ErrorEvent> current_error() const {
        return std::nullopt;
    }

  protected:
    /**
     * @brief The recovery buttons this backend offers for its current fault.
     *
     * The companion to classify_error() / current_error(): those decide *that*
     * there is a fault and what to call it, this decides what the user can tap.
     * Split out as its own hook because the action set is derived from live
     * backend state (is the toolhead loaded, which lane is selected) and is
     * therefore the same answer no matter which of the two entry points asked.
     *
     * @warning **The caller must already hold the backend's own mutex_.**
     *          Both existing overrides read mutex-protected state directly and
     *          take no lock of their own, and every call site is inside a
     *          `std::lock_guard<std::mutex> lock(mutex_)` scope in the same
     *          object. The mutexes are plain `std::mutex`, not recursive, so an
     *          override that locks — or a caller that does not — deadlocks.
     *          Overrides must document and preserve this.
     *
     * The base returns an EMPTY vector, and that is deliberate rather than a
     * placeholder: `decide_presentation()` (gcode_error_router.cpp) keys off
     * `recovery_actions.empty()` to choose MODAL vs MODAL_WITH_RECOVER, so any
     * guessed generic default here would silently promote every non-overriding
     * backend's plain error modal into a recovery prompt with buttons that
     * backend never vetted. Empty preserves today's behaviour exactly: a backend
     * offers recovery only by opting in.
     */
    [[nodiscard]] virtual std::vector<helix::RecoveryAction> build_recovery_actions() const {
        return {};
    }

  public:
    /// One ordered phase in a backend's toolchange narration model.
    struct ToolchangePhase {
        std::string id;    ///< stable key matched from narration, e.g. "brush"
        std::string label; ///< display label (translatable), e.g. "Brush nozzle"
        bool optional;     ///< if true, stays greyed/Pending when never narrated this swap
    };

    /// Declared ordered phase template for a toolchange operation.
    /// Empty (default) => backend has no narration model; the sidebar uses the
    /// legacy AmsAction-driven hardcoded step list (no regression).
    [[nodiscard]] virtual std::vector<ToolchangePhase>
    toolchange_phase_template(StepOperationType /*op*/) const {
        return {};
    }

    /// Map one `//` narration body (prefix already stripped) to a phase id.
    /// nullopt (default) => not a recognized phase line.
    ///
    /// A `//` body came from a macro's own respond_info, so implementations may
    /// use loose substring needles here and tolerate upstream rewording. Do NOT
    /// route bare console lines through this — see match_bare_narration_phase().
    [[nodiscard]] virtual std::optional<std::string>
    match_narration_phase(const std::string& /*narration*/) const {
        return std::nullopt;
    }

    /// Map one console line that arrived WITHOUT the `//` prefix to a phase id.
    /// nullopt (default) => backend narrates nothing outside `//`.
    ///
    /// Separate from match_narration_phase() because the input is a different
    /// kind of text: unprefixed responses are the printer's open console, mixing
    /// M105 reports, `echo:` output and USER-CONTROLLED gcode filenames in with
    /// any narration. Implementations must match anchored line shapes, never
    /// loose substrings — a needle like `cut` would fire on `haircut.gcode`.
    [[nodiscard]] virtual std::optional<std::string>
    match_bare_narration_phase(const std::string& /*line*/) const {
        return std::nullopt;
    }

    /// True when an UNMATCHED console line looks like this backend's narration,
    /// i.e. is worth a drift hint in the log. Deliberately looser than the two
    /// matchers — its whole job is catching wording this backend used to emit —
    /// but it must exclude the lines the backend emits that have no phase by
    /// design, or every operation reports itself as drift.
    /// false (default) => backend opts out of drift hints.
    [[nodiscard]] virtual bool is_narration_drift_candidate(const std::string& /*line*/) const {
        return false;
    }

    // ========================================================================
    // Operation Step Model (backend-driven step bar)
    // ========================================================================

    /// One ordered step in a backend's operation step bar.
    struct OperationStep {
        std::string label;      ///< display label (translatable)
        int phase_id = -1;      ///< backend phase index this step represents (-1 = positional)
        bool optional = false;  ///< stays greyed/Pending when never reached this op
        bool live_temp = false; ///< render a live "<label> cur/target°C" while current
    };

    /// Ordered step labels for an operation. Empty => backend has no specialized
    /// step model and the sidebar falls back to the legacy coarse AmsAction model.
    struct OperationStepModel {
        std::vector<OperationStep> steps;
    };

    /**
     * @brief Ordered step model for an operation type.
     *
     * Default builds from toolchange_phase_template(op): if the backend declares
     * a narration phase template the steps mirror it (label + optional). Backends
     * with no template inherit an empty model and the sidebar uses the legacy
     * coarse AmsAction->index fallback. Backends with a non-narration step source
     * (e.g. firmware phases) override this to supply their own labels.
     *
     * @param op  Operation being performed
     * @return ordered steps, or empty for the legacy fallback
     */
    [[nodiscard]] virtual OperationStepModel get_operation_step_model(StepOperationType op) const {
        OperationStepModel model;
        for (const auto& p : toolchange_phase_template(op)) {
            model.steps.push_back({p.label, -1, p.optional, false});
        }
        return model;
    }

    /**
     * @brief Subject the sidebar observes to learn the CURRENT step index.
     *
     * The value is the step index (-1 = none/idle). nullptr (or no specialized
     * model) => the sidebar derives the index from the coarse AmsAction map.
     *
     * Default: returns the toolchange narration step subject when the backend has
     * a non-empty phase template (the GcodeNarrationRouter drives it), else
     * nullptr. Backends with a firmware-phase source override this.
     *
     * The returned subject MUST be a static/singleton subject (the sidebar
     * observes it with a member ObserverGuard and no SubjectLifetime token).
     *
     * @param op  Operation being performed
     * @return subject pointer, or nullptr for the legacy fallback
     */
    [[nodiscard]] virtual lv_subject_t* get_operation_step_index_subject(StepOperationType op);

    /**
     * @brief True when this backend already populates remaining_weight_g from a live
     * printer-side source. FilamentConsumptionTracker skips slots on such backends
     * to avoid double-counting.
     */
    [[nodiscard]] virtual bool tracks_consumption_natively() const {
        return false;
    }

    /**
     * @brief Check if filament is currently loaded in extruder
     * @return true if filament is loaded
     */
    [[nodiscard]] virtual bool is_filament_loaded() const = 0;

    // ========================================================================
    // Filament Path Visualization
    // ========================================================================

    /**
     * @brief Get the path topology for this AMS system
     *
     * Determines how the filament path is rendered:
     * - LINEAR: Selector picks from multiple gates (Happy Hare ERCF)
     * - HUB: Multiple lanes merge through a hub (AFC Box Turtle)
     *
     * @return PathTopology enum value
     */
    [[nodiscard]] virtual PathTopology get_topology() const = 0;

    /**
     * @brief Get the path topology for a specific unit
     *
     * In mixed-topology systems (e.g., Box Turtle + OpenAMS), different units
     * may have different topologies. This method returns the topology for a
     * specific unit by index.
     *
     * Default implementation falls back to get_topology() for backward compat.
     *
     * @param unit_index Index of the unit (0-based)
     * @return PathTopology for this unit, or system-wide topology if unknown
     */
    [[nodiscard]] virtual PathTopology get_unit_topology(int unit_index) const {
        (void)unit_index;
        return get_topology();
    }

    /**
     * @brief Get current filament position in the path
     *
     * Returns which segment the filament is currently at/in.
     * Used for highlighting the active portion of the path visualization.
     *
     * @return PathSegment enum value (NONE if no filament in system)
     */
    [[nodiscard]] virtual PathSegment get_filament_segment() const = 0;

    /**
     * @brief Get filament position for a specific slot
     *
     * Returns how far filament from a specific slot extends into the path.
     * Used for visualizing all installed filaments, not just the active one.
     * For non-active slots, this typically shows filament up to the prep sensor.
     *
     * @param slot_index Slot index (0 to total_slots-1)
     * @return PathSegment enum value (NONE if no filament installed at slot)
     */
    [[nodiscard]] virtual PathSegment get_slot_filament_segment(int slot_index) const = 0;

    /**
     * @brief LIVE: filament present at this slot's toolhead/extruder.
     *
     * Reports the per-slot toolhead/motion-switch sensor state (e.g. the
     * Snapmaker per-tool `filament_motion_sensor`). This is the real-time signal
     * the panel observes to redraw the path the instant filament reaches (or
     * leaves) a slot's toolhead.
     *
     * Default false = "no per-slot toolhead sensor"; the panel falls back to the
     * slot's path segment / status. Backends override ONLY where the signal
     * genuinely exists in their parse — never fabricate.
     *
     * @param slot_index Slot index (0 to total_slots-1)
     * @return true if filament is detected at this slot's toolhead
     */
    [[nodiscard]] virtual bool slot_has_filament_at_toolhead(int slot_index) const {
        (void)slot_index;
        return false;
    }

    /**
     * @brief Whether this backend's parse carries per-slot loaded truth.
     *
     * Answers "does get_slot_info(i).status carry the seated answer for slot
     * i?" — either because the firmware publishes it per slot (AFC's
     * tool_loaded) or because the parse derives it there on every frame (CFS,
     * from the T{n}.filament letter plus the toolhead switch). Backends that
     * answer true have their per-slot status believed over the aggregate pair;
     * see slot_is_actively_loaded().
     *
     * Default false, and deliberately so. A backend that never marks the active
     * slot LOADED would report every slot unloaded and blank the active-lane
     * highlight — worse than the aggregate staleness this seam exists to fix.
     * Opt in only once the backend's parse genuinely sets SlotStatus::LOADED on
     * the seated slot, and cover it with a test that fails if the parse stops
     * doing so.
     *
     * Staying false is a legitimate answer, not a gap. Happy Hare's mmu.gate /
     * mmu.filament and Toolchanger's toolchanger.tool_number are firmware's own
     * single-valued statements, parsed verbatim into the aggregate pair; their
     * per-slot stamps are derived FROM it, so believing those back would only add
     * staleness. Toolchanger has no filament signal of any kind for a per-slot
     * rule to be authoritative about. See docs/devel/FILAMENT_MANAGEMENT.md
     * § "Per-Slot Load Authority".
     *
     * @return true if get_slot_info(i).status is authoritative for "loaded"
     */
    [[nodiscard]] virtual bool has_per_slot_loaded_authority() const {
        return false;
    }

    /**
     * @brief Firmware "seated & loaded" for this slot.
     *
     * The single source of truth for the active-lane highlight, replacing the
     * divergent badge/top-right reads, and (with
     * slot_has_filament_at_toolhead()) the gate on the Load/Unload affordances.
     *
     * Two rules, selected by has_per_slot_loaded_authority():
     *
     *  - Per-slot backends read the slot's own LOADED status. This is the
     *    truthful answer to a per-slot question and cannot disagree with itself
     *    across slots.
     *  - Everyone else derives it from the aggregate current_slot +
     *    filament_loaded pair. That derivation is only as good as our tracking
     *    of the active-slot pointer: when it names the wrong slot, or lags a
     *    toolchange, every affordance built on this predicate inherits the wrong
     *    answer (#1194 — Load stayed enabled on a lane AFC had already seated).
     *
     * Backends may still override outright where neither rule fits.
     *
     * @param slot_index Slot index (0 to total_slots-1)
     * @return true if firmware considers this slot seated and loaded
     */
    [[nodiscard]] virtual bool slot_is_actively_loaded(int slot_index) const {
        if (has_per_slot_loaded_authority()) {
            return get_slot_info(slot_index).status == SlotStatus::LOADED;
        }
        return slot_index == get_current_slot() && is_filament_loaded();
    }

    /**
     * @brief Infer which segment has an error
     *
     * When an error occurs, this determines which segment of the path
     * is most likely the problem area based on sensor states and
     * current operation. Used for visual error highlighting.
     *
     * @return PathSegment enum value (NONE if no error or can't determine)
     */
    [[nodiscard]] virtual PathSegment infer_error_segment() const = 0;

    /**
     * @brief Get bowden loading progress percentage
     *
     * Returns the firmware-reported bowden loading progress (0-100%).
     * Happy Hare v4 provides this via printer.mmu.bowden_progress.
     * When available (>= 0), AmsState uses it to drive path_anim_progress_subject
     * instead of UI-controlled animation.
     *
     * @return 0-100 for real progress, -1 if not available (v3 or non-HH backends)
     */
    [[nodiscard]] virtual int get_bowden_progress() const {
        return -1;
    }

    /**
     * @brief Check if a specific slot has a prep/pre-gate sensor
     *
     * Returns whether the given slot has a prep sensor that can detect
     * filament presence. Used by the path canvas to decide whether to
     * draw a prep sensor dot for each slot.
     *
     * Default implementation returns false (no prep sensor).
     *
     * @param slot_index Slot index (0 to total_slots-1)
     * @return true if slot has a prep/pre-gate sensor
     */
    [[nodiscard]] virtual bool slot_has_prep_sensor(int slot_index) const {
        (void)slot_index;
        return false;
    }

    // ========================================================================
    // Filament Operations
    // ========================================================================

    /**
     * @brief Does this backend's load/unload/tool-change macro home the toolhead
     *        by itself, inside firmware, where HelixScreen cannot see the G28?
     *
     * Decides whether a toolhead-motion filament op is refused during a PAUSED
     * print (see AmsSubscriptionBackend::refuse_if_printing). PRINTING is refused
     * for every backend regardless of this answer.
     *
     * Pausing to swap filament is the runout / colour-change recovery workflow,
     * not an edge case: Klipper's runout handler pauses and tells the user to
     * load filament and press RESUME, and `pause_resume` saves the gcode state so
     * the job resumes from where it left off. Mainsail offers unload on a paused
     * print and it works. AFC goes further — its own `is_printing()` is
     * `print_stats.state == "printing"` (paused is NOT printing, so AFC's
     * firmware guards permit the op), and AFC_PAUSE Z-hops the nozzle clear of
     * the part before handing off to the user's PAUSE macro
     * (AFC-Klipper-Add-On `extras/AFC_error.py` cmd_AFC_PAUSE).
     *
     * What is NOT safe is a firmware macro that buries its own home. On
     * loadcell-Z printers a G28 probes the nozzle DOWN into the bed; issued while
     * a job owns the toolhead that is a collision, and on AD5X/ZMOD it trips
     * ZCONTROL_AUTO into a Klipper shutdown needing a firmware restart to recover
     * (bundle XWPBR2DX, commit 329e731e9).
     *
     * HelixScreen's OWN homing is already handled without this flag, in two
     * layers that both remain in force:
     *   - Layer 1: helix::api::reject_homing_during_active_print() refuses any
     *     app-emitted G28 while PRINTING or PAUSED, in MoonrakerAPI::execute_gcode
     *     and MoonrakerMotionAPI::execute_gcode.
     *   - AmsSubscriptionBackend::ensure_homed_then() only emits G28 when
     *     toolhead.homed_axes lacks "xyz" — and a paused print is homed by
     *     construction, so it emits nothing.
     * This flag therefore covers exactly one thing: homes Layer 1 cannot see.
     *
     * Default: false. Override true ONLY with positive evidence that the specific
     * firmware macro this backend dispatches homes on its own; "not sure" must
     * stay false, because a permanent false refusal is its own broken workflow
     * (bundle JX2FVRB9).
     */
    [[nodiscard]] virtual bool filament_ops_self_home() const {
        return false;
    }

    /**
     * @brief Record that the user has already agreed to a pre-operation home for
     *        the NEXT dispatch, so ensure_homed_then() does not ask a second
     *        time.
     *
     * Armed by a UI surface that asks before starting its own preheat (moving
     * the "home printer first?" question ahead of the preheat instead of after
     * it, so a decline never wastes a heat cycle). Single-shot: the backend
     * consumes it on the very next ensure_homed_then() call that finds the
     * toolhead genuinely unhomed. Does NOT skip the G28 itself -- only the
     * prompt. Default no-op for backends that don't route through
     * AmsSubscriptionBackend::ensure_homed_then().
     */
    virtual void arm_home_preconfirmed() {}

    /**
     * @brief Clear a previously armed pre-confirmation without consuming it via
     *        a dispatch.
     *
     * Call when a confirmed-but-not-yet-dispatched load is abandoned --
     * preheat cancelled, the panel torn down, the operation aborted -- so
     * consent does not leak forward into a later, unrelated operation on this
     * backend. Safe to call whether or not anything is currently armed.
     * Default no-op, mirroring arm_home_preconfirmed().
     */
    virtual void clear_home_preconfirmed() {}

    /**
     * @brief Whether the UI should redirect to the AMS panel for slot selection
     *        before loading filament.
     *
     * When true, the filament panel navigates to the AMS management UI so the
     * user can pick a specific slot. When false, the UI falls through to the
     * standard LOAD_FILAMENT macro or raw G-code (e.g. bypass mode where the
     * user is feeding filament directly).
     *
     * Default: true (most backends need slot selection). Override in backends
     * where bypass or other modes allow loading without slot selection.
     */
    [[nodiscard]] virtual bool requires_slot_selection_for_load() const {
        return !is_bypass_active();
    }

    /**
     * @brief Whether a load operation must first unload/cut the currently
     *        present filament (load-vs-swap decision).
     *
     * Returns true when filament is physically at the nozzle (or, for most
     * backends, when a slot is otherwise reported as engaged) so the caller
     * picks the cut-before-load (swap) path instead of a fresh load. Centralized
     * here so the UI and backends agree on the rule and per-variant quirks
     * (e.g. K1 CFS preloads current_slot with an empty nozzle) live in one place.
     *
     * Default: filament at nozzle OR a slot engaged. Override where the
     * current_slot signal does not imply filament at the nozzle.
     *
     * A slot on an independent path is answered false outright — see
     * slot_has_independent_path(). The default rule encodes a SERIAL premise
     * (one shared route to the nozzle, so clear it before another lane can feed)
     * and a lane whose toolhead owns its own extruder shares nothing to clear.
     * Loading tool 3 never requires unloading tool 1.
     *
     * That is not academic on either uniformly-PARALLEL backend, because both
     * report current_slot from the mounted tool and so answered true permanently:
     *   - Snapmaker: routed Load through change_tool() -> `T{n}`, which seats the
     *     carriage and feeds nothing. load_filament() names that as its first
     *     wrong answer; AUTO_FEEDING already targets any extruder directly.
     *   - Toolchanger: the swap arm dispatches change_tool(mapped_tool), but
     *     change_tool() validates its argument as a SLOT and emits
     *     `SELECT_TOOL T={n}` precisely to bypass ASSIGN_TOOL remapping — so a
     *     remapped tool would mount the wrong physical toolhead.
     * This is the PARALLEL-appropriate rule deferred in #1199.
     *
     * @param info        Backend system info snapshot.
     * @param target_slot Global index of the slot the user asked to load. Pass
     *                    -1 when no slot is resolved; the per-lane refinement is
     *                    skipped and the backend-wide topology answers.
     */
    [[nodiscard]] virtual bool needs_unload_before_load(const AmsSystemInfo& info,
                                                        int target_slot) const {
        if (slot_has_independent_path(info, target_slot)) {
            return false;
        }
        return info.filament_loaded || info.current_slot >= 0;
    }

    /**
     * @brief Does @p target_slot reach a nozzle by a route it shares with no
     *        other lane?
     *
     * The question load-vs-swap actually turns on, and it is per-LANE, not
     * per-backend. PathTopology::MIXED exists for exactly this: a unit with some
     * lanes wired straight to their own extruder and others merged through a hub
     * into a shared one. On such a unit the answer differs between two lanes of
     * the same unit, so no backend-wide constant can express it.
     *
     * Three sources, most specific first:
     *   1. AmsUnit::lane_is_hub_routed — AFC's per-lane `hub` field ("direct" /
     *      "direct_load" vs a hub name). Consulted ONLY on a MIXED unit: the
     *      vector stores `false` for lanes whose routing has not been parsed yet
     *      (ams_backend_afc.cpp, #1229 defect 4), and on a uniform unit that
     *      unknown would masquerade as "direct" and skip a swap the machine
     *      needs. On a MIXED unit an out-of-range or unknown lane answers false
     *      (shared), which is the conservative direction.
     *   2. get_unit_topology(position) — the backend's own per-unit answer. Its
     *      default falls back to get_topology(), so backends that never populate
     *      AmsUnit::topology (ToolChanger) still answer correctly.
     *   3. get_topology() — when no unit covers @p target_slot at all, including
     *      target_slot < 0.
     *
     * @warning Reaches a virtual accessor that may take the backend's own mutex
     *          (AmsBackendAfc::get_unit_topology does). Do not call it, or
     *          needs_unload_before_load(), while holding that mutex.
     *          AmsBackendCfs::change_tool() is the one backend-internal caller
     *          and is safe: CFS does not override get_unit_topology(), and its
     *          get_topology() is an inline constant.
     */
    [[nodiscard]] bool slot_has_independent_path(const AmsSystemInfo& info, int target_slot) const {
        const int unit_pos = info.get_unit_position_for_slot(target_slot);
        if (unit_pos < 0) {
            return get_topology() == PathTopology::PARALLEL;
        }

        const PathTopology topology = get_unit_topology(unit_pos);
        if (topology != PathTopology::MIXED) {
            return topology == PathTopology::PARALLEL;
        }

        const AmsUnit& unit = info.units[static_cast<size_t>(unit_pos)];
        const int lane = target_slot - unit.first_slot_global_index;
        if (lane < 0 || lane >= static_cast<int>(unit.lane_is_hub_routed.size())) {
            return false;
        }
        return !unit.lane_is_hub_routed[static_cast<size_t>(lane)];
    }

    // ========================================================================
    // Filament Operations
    //
    // Every real backend derives from AmsSubscriptionBackend, which implements
    // these four as a non-virtual interface: it marks them `final`, runs the
    // print-active gate, and dispatches to a protected do_* hook the backend
    // writes instead. So a subscription backend does not implement these
    // directly and cannot skip the gate. Only AmsBackendMock, which has no
    // MoonrakerAPI and therefore no print state to consult, overrides them here.
    // ========================================================================

    /**
     * @brief Load filament from specified slot (async)
     *
     * Initiates filament load from the specified slot to the extruder.
     * Results delivered via EVENT_LOAD_COMPLETE or EVENT_ERROR.
     *
     * Requires:
     * - System not busy with another operation
     * - Slot has filament available
     * - Extruder at appropriate temperature
     *
     * @param slot_index Slot to load from (0-based)
     * @return AmsError indicating if operation was started successfully
     */
    virtual AmsError load_filament(int slot_index) = 0;

    /**
     * @brief Unload filament from a specific slot (async)
     *
     * Initiates filament unload from extruder back to its slot.
     * Results delivered via EVENT_UNLOAD_COMPLETE or EVENT_ERROR.
     *
     * @param slot_index Slot to unload. MUST be explicit — there is no default.
     *        Callers that mean "whatever is active" should call
     *        unload_active_filament() instead, which resolves current_slot once
     *        (single source of truth) and forwards here. Backends may still
     *        receive slot_index < 0 via that path when current_slot is unknown
     *        (no active tool); each backend's override documents its -1 behavior.
     *
     * Requires:
     * - Filament currently loaded
     * - System not busy with another operation
     * - Extruder at appropriate temperature
     *
     * @return AmsError indicating if operation was started successfully
     */
    virtual AmsError unload_filament(int slot_index) = 0;

    /**
     * @brief Unload whichever slot the backend currently considers active.
     *
     * Convenience that resolves the active slot from system_info_.current_slot
     * ONCE in the base class and forwards to unload_filament(int). Single
     * source of truth for the "unload active" semantic — eliminates the
     * per-backend "if (slot_index < 0) slot = current_slot" fallback that
     * previously lived in each unload_filament override.
     *
     * That fallback let a callsite's snapshot of current_slot (read for a
     * "is anything loaded?" guard) diverge from the backend's re-read inside
     * unload_filament — causing the U1 Filament-panel-unload wrong-tool bug
     * (Helix sent EXTRUDER=0 because the backend's stale read won over the
     * UI's fresh one).
     *
     * If current_slot is -1 (no active slot known), -1 is forwarded to the
     * backend override, which keeps its own per-backend "trust the firmware"
     * behavior (Snapmaker: bare INNER_FILAMENT_UNLOAD, Toolchanger: not_loaded
     * error, AFC: bare TOOL_UNLOAD, etc).
     */
    AmsError unload_active_filament();

    /**
     * @brief Select tool/slot without loading (async)
     *
     * Moves the selector to the specified slot without loading filament.
     * Used for preparation or manual operations.
     *
     * @param slot_index Slot to select (0-based)
     * @return AmsError indicating if operation was started successfully
     */
    virtual AmsError select_slot(int slot_index) = 0;

    /**
     * @brief Perform tool change (async)
     *
     * Complete tool change sequence: unload current, load new.
     * Equivalent to sending T{tool_number} command.
     * Results delivered via EVENT_TOOL_CHANGED or EVENT_ERROR.
     *
     * @param tool_number Tool to change to (0-based)
     * @return AmsError indicating if operation was started successfully
     */
    virtual AmsError change_tool(int tool_number) = 0;

    // ========================================================================
    // Recovery Operations
    // ========================================================================

    /**
     * @brief Attempt recovery from error state
     *
     * Initiates system recovery procedure appropriate to current error.
     * For Happy Hare, this typically invokes MMU_RECOVER.
     *
     * @return AmsError indicating if recovery was started
     */
    virtual AmsError recover() = 0;

    /**
     * @brief Reset the AMS system (async)
     *
     * Resets the system to a known good state.
     * - Happy Hare: Calls MMU_HOME to home the selector
     * - AFC: Calls AFC_RESET to reset the system
     *
     * @return AmsError indicating if operation was started
     */
    virtual AmsError reset() = 0;

    /**
     * @brief Clear a latched fault so the system stops reporting an error
     *
     * Bookkeeping only — this never moves filament. Distinct from
     * recover_lane_position(), which is a physical retract.
     *
     * Scope is backend-defined. AFC has no per-lane fault clear, so it ignores
     * slot_index and clears system-wide (RESET_FAILURE + AFC_CLEAR_MESSAGE).
     * Happy Hare clears per-gate (MMU_RECOVER GATE=n). Callers always pass the
     * slot they mean and let the backend decide what it can honour.
     *
     * Must be safe to call from IDLE: a latched fault routinely outlives the
     * operation that produced it, which is precisely when clearing matters.
     *
     * @param slot_index Slot the user acted on, or -1 for "no particular slot"
     * @return AmsError indicating if the operation was started
     */
    virtual AmsError clear_fault(int slot_index) {
        (void)slot_index;
        return cancel();
    }

    /**
     * @brief Retract a lane's filament back to its lane from the bowden
     *
     * A physical filament move, not a fault clear. Recovers a lane left stranded
     * mid-path by a failed load or unload — filament past the lane but not at the
     * toolhead, which plain unload() cannot address because it assumes the head.
     *
     * AFC: AFC_LANE_RESET LANE={name}, which retracts until the hub clears.
     * Default implementation returns NOT_SUPPORTED.
     *
     * @param slot_index Lane to recover (0-based)
     * @return AmsError indicating if the operation was started
     */
    virtual AmsError recover_lane_position(int slot_index) {
        (void)slot_index;
        return AmsErrorHelper::not_supported("Lane position recovery not supported");
    }

    /**
     * @brief Whether a lane-position recovery is possible for this slot right now
     *
     * Per-slot and per-state, not a static capability: offering a recovery the
     * firmware will refuse produces an error the user cannot act on, and on AFC
     * that error latches in printer.AFC.message and keeps re-firing toasts.
     */
    [[nodiscard]] virtual bool can_recover_lane_position(int slot_index) const {
        (void)slot_index;
        return false;
    }

    /**
     * @brief Whether this backend currently knows WHICH lane needs recovery
     *
     * Distinct from can_recover_lane_position(), which answers "is recovery
     * possible" per slot. This answers "do we know whose fault it is" — some
     * backends share one physical sensor across every lane on a unit (AFC's hub
     * sensor), so an unattributed trigger cannot say whose filament caused it.
     * Callers use this to decide whether RecoverPosition should outrank Eject
     * (attributed: confident, single lane) or defer to it (unattributed: a
     * last-resort offer, since showing Recover on every lane would otherwise
     * hide Eject from lanes that are simply seated, not stranded).
     *
     * A backend MAY choose to make can_recover_lane_position() imply this, by
     * refusing recovery outright where it cannot attribute the strand. AFC does
     * (prestonbrown/helixscreen#1182): its lane reset opens with a blind retract,
     * so a wrong guess de-seats a working lane rather than refusing harmlessly.
     * The unattributed arm in the caller's ranking stays valid for backends
     * whose recovery is genuinely free to attempt.
     *
     * Default false: backends with a genuinely per-lane fault signal (or no
     * lane-position recovery at all) have nothing to attribute.
     */
    [[nodiscard]] virtual bool lane_recovery_is_attributed() const {
        return false;
    }

    /**
     * @brief Eject filament from a specific lane (async)
     *
     * Reverses the lane's extruder motor to release filament so the spool
     * can be physically removed. Different from unload_filament() which
     * retracts filament from the toolhead back through the hub.
     *
     * For AFC: sends LANE_UNLOAD LANE={name}
     * Default implementation returns NOT_SUPPORTED.
     *
     * @param slot_index Lane to eject from (0-based)
     * @return AmsError indicating if operation was started
     */
    virtual AmsError eject_lane(int slot_index) {
        (void)slot_index;
        return AmsErrorHelper::not_supported("Lane eject not supported");
    }

    /**
     * @brief Check if per-lane eject is supported
     * @return true if eject_lane() is implemented
     */
    [[nodiscard]] virtual bool supports_lane_eject() const {
        return false;
    }

    /**
     * @brief Check if eject_lane() works even when the lane reports EMPTY/runout
     *
     * True if eject_lane() works even when the lane reports EMPTY/runout — i.e. a
     * cold retract that ignores the presence sensor, used to recover a snapped
     * chunk stuck in an idle lane. Distinct from supports_lane_eject() (eject an
     * idle lane that still has filament present).
     *
     * @return true if a cold, sensor-ignoring eject is available
     */
    [[nodiscard]] virtual bool supports_force_eject() const {
        return false;
    }

    /**
     * @brief Whether this backend's COLD lane ops are themselves refused mid-print
     *
     * The cold lane ops (Eject / Recover / ForceEject) move no toolhead, so the
     * shared affordance rule deliberately exempts them from the print gate that
     * blocks a heated unload — see OpButtonState::unload_is_cold_lane_op. That
     * exemption is about OUR gate. It says nothing about whether the firmware on
     * the other end will accept the command.
     *
     * AFC does not: `cmd_LANE_UNLOAD` opens with
     * `if self.function.is_printing(): AFC_error(...); return` on every version
     * shipped (v1.1.0 AFC.py:1112, v1.2.0 AFC.py:1331). A backend that answers
     * true here keeps its cold ops greyed while the print gate is closed, so the
     * button is not offered into a guaranteed refusal.
     *
     * Default false: for every other backend the exemption is correct, and the
     * cold ops stay reachable mid-pause for clearing a snapped strand.
     *
     * @return true if a print blocks this backend's cold lane ops too
     */
    [[nodiscard]] virtual bool cold_lane_ops_refused_during_print() const {
        return false;
    }

    /**
     * @brief Whether the slot can be unloaded from the toolhead.
     *
     * Gates the context-menu "Unload" action, and (inverted) also suppresses the
     * "Load" action for the same slot: a slot the firmware still considers
     * seated/active should not offer Load.
     *
     * The default rule is topology-aware so every backend behaves consistently
     * without per-backend duplication:
     *
     *  - PARALLEL toolchangers give each tool its own independent toolhead, so
     *    any tool that currently holds filament is independently unloadable. We
     *    key on is_present() — the same presence signal the menu's Load button
     *    uses — so Load and Unload always agree.
     *  - Selector / hub MMUs (Happy Hare, AFC, ACE, CFS, AD5X, QIDI) share one
     *    extruder, so only the slot actually seated at the toolhead (LOADED) can
     *    be unloaded.
     *
     * Note that BOTH PARALLEL backends override the first arm, for opposite
     * reasons, so it is a fallback rather than a rule in force today. Snapmaker
     * U1 needs its channel_state latch because the per-tool motion sensor stays
     * true after an unload. Generic ToolChanger needs the narrower
     * `slot_index == current_tool`: its slots are physical toolheads that are
     * never EMPTY, so is_present() read true everywhere, which suppressed Load on
     * every tool and offered an unmount on tools sitting in their docks (#1199).
     *
     * AD5X IFS also overrides this so a runout that clears the head sensor
     * doesn't disable Unload on the slot the firmware reports as active — the
     * exact moment the user needs to recover (#995). Its base fallback is
     * unchanged: AD5X is a serial topology, so the rule below still yields
     * status == LOADED for it.
     *
     * Action gating only — display status is unaffected.
     *
     * @param slot_index Slot to query (0-based)
     * @return true if Unload should be offered (and Load suppressed) for this slot
     */
    [[nodiscard]] virtual bool can_unload_from_toolhead(int slot_index) const {
        const SlotInfo slot = get_slot_info(slot_index);
        if (get_topology() == PathTopology::PARALLEL) {
            return slot.is_present();
        }
        return slot.status == SlotStatus::LOADED;
    }

    /**
     * @brief Whether the unload action on this slot performs a heated toolhead
     *        unload (true) versus a cold per-lane eject (false).
     *
     * The context menu uses this to label the action "Unload" vs "Eject" and to
     * dispatch the matching request. The default mirrors the legacy rule — a
     * toolhead unload only when the menu already considers the slot loaded.
     * AD5X IFS overrides it with seated-channel (IFS_STATUS Chan) authority so a
     * NON-seated lane reads "Eject" even when the firmware has dropped its
     * active-slot pointer and the recovery-broadened loaded_hint says otherwise.
     *
     * @param slot_index  Slot to query (0-based)
     * @param loaded_hint The menu's computed is_loaded snapshot for this slot
     * @return true if a heated toolhead unload should be offered/dispatched
     */
    [[nodiscard]] virtual bool slot_unloads_to_toolhead(int slot_index, bool loaded_hint) const {
        (void)slot_index;
        return loaded_hint;
    }

    /**
     * @brief Which unit owns this slot's filament IDENTITY, when not this one.
     *
     * On multiACE a U1 head is fed from an ACE bay, so the head's material,
     * colour and Spoolman link are the ACE's to state. Editing them on the head
     * would write `print_task_config` and be overwritten the moment the ACE
     * reports its inventory again — two sources of truth for one spool. The
     * slot menu uses this to drop its edit actions and offer a route to the
     * owning unit instead.
     *
     * Identity only. Loading and unloading still act on the head and stay
     * available; this says who describes the filament, not who moves it.
     *
     * @param slot_index Global slot index.
     * @return Owning unit index, or nullopt when the slot describes itself
     *         (every backend except multiACE, always).
     */
    [[nodiscard]] virtual std::optional<int> slot_identity_owner_unit(int slot_index) const {
        (void)slot_index;
        return std::nullopt;
    }

    /**
     * @brief Global indices of the slots that hold a spool of their OWN, in order.
     *
     * A slot fed from another unit is not a spool position — it is a view of one.
     * On multiACE, the U1's ACE-fed head and the ACE bay behind it are a single
     * physical spool, so counting both double-counts it: a 4-head U1 with one
     * 4-bay ACE has 7 spool positions, not 8.
     *
     * This is what user-facing COUNTS and per-spool rows should iterate.
     * `total_slots` remains the indexing bound and is unchanged — every slot
     * here is still addressable, still loadable, still unloadable.
     *
     * Derived from slot_identity_owner_unit(), so a backend that answers that
     * question needs nothing further.
     */
    [[nodiscard]] std::vector<int> owned_spool_slots() const {
        std::vector<int> out;
        const AmsSystemInfo info = get_system_info();
        out.reserve(static_cast<size_t>(info.total_slots));
        for (int i = 0; i < info.total_slots; ++i) {
            if (!slot_identity_owner_unit(i).has_value()) {
                out.push_back(i);
            }
        }
        return out;
    }

    /**
     * @brief How many slots on @p unit_index hold a spool of their own.
     * @see owned_spool_slots()
     */
    [[nodiscard]] int unit_spool_slot_count(int unit_index) const {
        const AmsSystemInfo info = get_system_info();
        if (unit_index < 0 || unit_index >= static_cast<int>(info.units.size())) {
            return 0;
        }
        const auto& unit = info.units[unit_index];
        int n = 0;
        for (int s = 0; s < unit.slot_count; ++s) {
            if (!slot_identity_owner_unit(unit.first_slot_global_index + s).has_value()) {
                ++n;
            }
        }
        return n;
    }

    /**
     * @brief Whether the backend can dock the mounted toolhead without unloading.
     * @return true if park_toolhead() is implemented (toolchangers only).
     */
    [[nodiscard]] virtual bool supports_toolhead_park() const {
        return false;
    }

    /**
     * @brief Return the mounted toolhead to its dock, leaving filament alone.
     *
     * Parking is a carriage operation, not a filament one: on a toolchanger a
     * head is routinely docked with filament still threaded to its nozzle, and
     * the next pick-up finds it exactly as it was. So this must NOT unload —
     * the two are separate actions with separate menu entries.
     *
     * Only meaningful while a head is actually mounted; with an empty carriage
     * there is nothing to dock. Callers gate on MountState/mounted_tool.
     *
     * @warning This is a plain virtual — it does NOT pass through the
     * run_filament_op() gate that load/unload/select get for free. Parking
     * moves the carriage, so an implementation MUST apply its own print refusal
     * (check_preconditions(true)) or it will dock the head mid-print.
     *
     * @return AmsError indicating success or failure.
     */
    virtual AmsError park_toolhead() {
        return AmsErrorHelper::not_supported("Toolhead park");
    }

    /**
     * @brief Whether the backend can position the selector at a gate without loading.
     * @return true if select_gate() is implemented (selector-based systems only).
     */
    [[nodiscard]] virtual bool supports_gate_select() const {
        return false;
    }

    /**
     * @brief Move the selector to a gate without loading filament.
     *
     * Positions the MMU selector at the given gate without loading filament
     * into the toolhead. This is useful for manual interventions and gate
     * inspection on selector-based systems.
     *
     * @param slot_index Zero-based gate index.
     * @return AmsError indicating success or failure.
     */
    virtual AmsError select_gate(int slot_index) {
        (void)slot_index;
        return AmsErrorHelper::not_supported("Gate select not supported");
    }

    /**
     * @brief Jog the selector relative to its current gate.
     *
     * Moves the selector to (current gate + delta), clamped to the valid range
     * [0, slot_count - 1], without loading filament. This is convenient for
     * stepping the selector one gate at a time during manual interventions.
     * Gated by supports_gate_select() — selector-based systems only.
     *
     * @param delta Signed number of gates to move (e.g. +1, -1).
     * @return AmsError indicating success or failure.
     */
    virtual AmsError move_selector(int delta) {
        (void)delta;
        return AmsErrorHelper::not_supported("Selector jog");
    }

    /**
     * @brief Whether the backend can probe gate sensors (MMU_CHECK_GATE).
     * @return true if check_gate()/check_all_gates() are implemented.
     */
    [[nodiscard]] virtual bool supports_gate_check() const {
        return false;
    }

    /**
     * @brief Probe a single gate's filament sensor (MMU_CHECK_GATE GATE=n).
     *
     * Asks the firmware to check whether filament is present at the specified
     * gate's sensor. Useful for diagnosing gate-status discrepancies without
     * a full load/unload cycle.
     *
     * Default implementation returns NOT_SUPPORTED.
     *
     * @param slot_index Zero-based gate index.
     * @return AmsError indicating success or failure.
     */
    virtual AmsError check_gate(int slot_index) {
        (void)slot_index;
        return AmsErrorHelper::not_supported("Gate check not supported");
    }

    /**
     * @brief Probe all gate sensors at once (MMU_CHECK_GATE, no params).
     *
     * Asks the firmware to check filament presence at every gate sensor
     * in one pass. Useful for a full gate-status audit.
     *
     * Default implementation returns NOT_SUPPORTED.
     *
     * @return AmsError indicating success or failure.
     */
    virtual AmsError check_all_gates() {
        return AmsErrorHelper::not_supported("Gate check not supported");
    }

    /**
     * @brief Display label for the sidebar reset()-button.
     *
     * Backends where reset() is a genuine reset keep "Reset"; selector-based
     * systems where reset() homes the mechanism (Happy Hare -> MMU_HOME) override
     * this to "Home" so the word "Reset" does not collide with the recover/re-sync
     * concept.
     * @return untranslated label key.
     */
    [[nodiscard]] virtual std::string reset_button_label() const {
        return "Reset";
    }

    /**
     * @brief Cancel current operation
     *
     * Attempts to safely abort the current operation.
     * Not all operations can be cancelled.
     *
     * @return AmsError indicating if cancellation was accepted
     */
    virtual AmsError cancel() = 0;

    // ========================================================================
    // Resume Preparation
    // ========================================================================

    /**
     * @brief Completion callback for prepare_for_resume()
     *
     * Always invoked on the main thread. Pass an AmsError with
     * AmsResult::SUCCESS to proceed with RESUME; any other result aborts
     * the resume sequence and surfaces a user-facing error.
     */
    using ResumeReadyCallback = std::function<void(const AmsError&)>;

    /**
     * @brief Backend-side preparation hook fired before a print Resume.
     *
     * Lets a backend run any device-specific recovery gcode (heating,
     * motor-engagement, encoder priming, etc.) before the caller dispatches
     * the RESUME macro. Implementations may detect their own stuck states
     * and either run a recovery sequence or no-op. The default implementation
     * is a no-op that invokes the callback immediately — appropriate when
     * Klipper's stock RESUME path is sufficient.
     *
     * Contract: on_ready is always invoked exactly once. It fires on the
     * main thread regardless of where the backend's gcode execution lands.
     * Pass AmsResult::SUCCESS to indicate the caller may proceed with
     * RESUME; any other result aborts the resume sequence and surfaces a
     * user-facing error.
     *
     * @param slot_index Active slot the caller intends to resume on
     *                   (-1 if no AMS context — backend may fall back to
     *                   whatever it considers "current")
     * @param on_ready   Callback fired when preparation is complete or
     *                   has failed
     */
    virtual void prepare_for_resume(int slot_index, ResumeReadyCallback on_ready) {
        (void)slot_index;
        if (on_ready) {
            on_ready(AmsErrorHelper::success());
        }
    }

    /**
     * @brief True when the current runout signal looks like a stale-sensor
     *        false positive rather than a genuine filament-out.
     *
     * On Snapmaker U1 the encoder-based motion sensor latches
     * filament_detected=false whenever no extrusion has happened recently —
     * so it can fire pause_on_runout at print start, before filament has
     * physically moved. The port/buffer sensor at the spool side still reads
     * filament present in that case. Callers use this to suppress the runout
     * guidance modal and auto-trigger prepare_for_resume + RESUME silently.
     *
     * Default: false (treat every runout signal as real).
     *
     * @param slot_index Slot to check (typically current active slot)
     */
    [[nodiscard]] virtual bool is_stuck_motion_sensor_runout(int slot_index) const {
        (void)slot_index;
        return false;
    }

    // ========================================================================
    // Configuration Operations
    // ========================================================================

    /**
     * @brief Update slot filament information
     *
     * Sets the color, material, and other filament info for a slot.
     *
     * When persist=true (default), changes are written to firmware via G-code
     * commands (e.g., SET_COLOR, SET_MATERIAL, SET_SPOOL_ID for AFC) so they
     * survive reboots. Use this for user-initiated edits.
     *
     * When persist=false, only in-memory state is updated and EVENT_SLOT_CHANGED
     * is emitted for UI refresh. This MUST be used when updating slots from
     * external data sources (e.g., Spoolman weight polling) to prevent a feedback
     * loop: set_slot_info(persist=true) → G-code → firmware status update →
     * sync_from_backend → refresh_spoolman_weights → set_slot_info again → ∞.
     * On AFC with 4 lanes this loop fires 16+ G-code commands per cycle and
     * saturates the CPU.
     *
     * @param slot_index Slot to update (0-based)
     * @param info New slot information (only filament fields used)
     * @param persist If true, persist changes to firmware. If false, update
     *               in-memory state only (for external data sync).
     * @return AmsError indicating if update succeeded
     */
    virtual AmsError set_slot_info(int slot_index, const SlotInfo& info, bool persist = true) = 0;

    /**
     * @brief Persist only a slot's filament weight (consumption tracking)
     *
     * Called by the consumption sink once per metered delta during a print.
     * Unlike set_slot_info(), this updates ONLY remaining/total weight and MUST
     * NOT touch material, color, or user-lock state, and MUST NOT re-emit any
     * firmware-facing color/material write. An automated weight tracker has no
     * business asserting filament identity — doing so clobbers an externally
     * changed material on backends that round-trip identity through a shared
     * firmware store (#981, AD5X native ZMOD: a 60 s weight persist rewrote
     * ffmType and reverted the user's material).
     *
     * The default routes through set_slot_info() — correct for backends where
     * weight and identity share one persist path with no clobber risk. Backends
     * that write identity to a firmware-owned store override this to persist
     * weight alone (see AmsBackendAd5xIfs).
     *
     * @param slot_index Slot to update (0-based)
     * @param remaining_weight_g New remaining weight in grams (>= 0)
     * @param total_weight_g Total weight in grams, or < 0 to leave unchanged
     * @param persist If true, persist to the slot's durable store; else in-memory only
     */
    virtual void update_slot_weight(int slot_index, float remaining_weight_g, float total_weight_g,
                                    bool persist) {
        SlotInfo info = get_slot_info(slot_index);
        info.remaining_weight_g = remaining_weight_g;
        if (total_weight_g >= 0.0f)
            info.total_weight_g = total_weight_g;
        set_slot_info(slot_index, info, persist);
    }

    /**
     * @brief Set tool-to-slot mapping
     *
     * Configures which slot a tool number maps to.
     * Happy Hare specific - may not be supported on all backends.
     *
     * @param tool_number Tool number (0-based)
     * @param slot_index Slot to map to (0-based)
     * @return AmsError indicating if mapping was set
     */
    virtual AmsError set_tool_mapping(int tool_number, int slot_index) = 0;

    /**
     * @brief Erase the user-provided override for a slot.
     *
     * Removes the FilamentSlotOverride for @p slot_index from both the
     * in-memory map and the persisted FilamentSlotOverrideStore, then refreshes
     * override-exclusive fields on the live SlotInfo so the cleared state is
     * visible via get_slot_info() on the very next read.
     *
     * Default implementation is a no-op. Backends without FilamentSlotOverride
     * integration (AFC, Happy Hare, Tool Changer, Mock) ignore the call.
     *
     * Safe to call from the UI thread. Backends lock their own mutex_ for the
     * in-memory mutation and submit the store clear asynchronously.
     *
     * @param slot_index Slot to clear (0-based, global)
     */
    virtual void clear_slot_override(int slot_index) {
        (void)slot_index;
    }

    // ========================================================================
    // Bypass Mode Operations
    // ========================================================================

    /**
     * @brief Enable bypass mode
     *
     * Activates bypass mode where an external spool feeds directly to the
     * toolhead, bypassing the MMU/hub system. Sets current_slot to -2.
     *
     * Not all backends support bypass mode - check supports_bypass flag.
     *
     * @return AmsError indicating if bypass was enabled
     */
    virtual AmsError enable_bypass() = 0;

    /**
     * @brief Disable bypass mode
     *
     * Deactivates bypass mode. Filament should be unloaded from toolhead first.
     *
     * @return AmsError indicating if bypass was disabled
     */
    virtual AmsError disable_bypass() = 0;

    /**
     * @brief Check if bypass mode is currently active
     * @return true if bypass is active (current_slot == -2)
     */
    [[nodiscard]] virtual bool is_bypass_active() const = 0;

    // ========================================================================
    // Dryer Control (Optional - default implementations return "not supported")
    // ========================================================================

    /**
     * @brief Get dryer state and capabilities
     *
     * Returns current dryer state including temperature, duration, and
     * hardware capabilities. Not all AMS systems have dryers - check
     * DryerInfo::supported before showing dryer UI.
     *
     * @return DryerInfo struct (supported=false if no dryer)
     */
    [[nodiscard]] virtual DryerInfo get_dryer_info(int unit = 0) const {
        (void)unit;
        return DryerInfo{.supported = false};
    }

    /**
     * @brief Start drying operation
     *
     * Initiates filament drying at specified temperature and duration.
     * Not all AMS systems support drying - check get_dryer_info().supported.
     *
     * @param temp_c Target temperature in Celsius (within min_temp_c..max_temp_c)
     * @param duration_min Drying duration in minutes (positive, capped at max_duration_min)
     * @param fan_pct Fan speed percentage (0-100, -1 = use backend default)
     * @return AmsError with SUCCESS result on success, or error with reason
     */
    virtual AmsError start_drying(float temp_c, int duration_min, int fan_pct = -1, int unit = 0) {
        (void)temp_c;
        (void)duration_min;
        (void)fan_pct;
        (void)unit;
        return AmsErrorHelper::not_supported("Dryer");
    }

    /**
     * @brief Stop drying operation
     *
     * Stops any active drying and turns off heater/fan.
     *
     * @return AmsError with SUCCESS result on success, or error with reason
     */
    virtual AmsError stop_drying(int unit = 0) {
        (void)unit;
        return AmsErrorHelper::not_supported("Dryer");
    }

    /**
     * @brief Update drying parameters while running
     *
     * Adjusts temperature, duration, or fan speed during an active dry cycle.
     * Pass -1 to keep current value for any parameter.
     *
     * @param temp_c New target temperature (-1 = no change)
     * @param duration_min New duration (-1 = no change)
     * @param fan_pct New fan speed (-1 = no change)
     * @return AmsError with SUCCESS result on success, or error with reason
     */
    virtual AmsError update_drying(float temp_c = -1, int duration_min = -1, int fan_pct = -1,
                                   int unit = 0) {
        (void)temp_c;
        (void)duration_min;
        (void)fan_pct;
        (void)unit;
        return AmsErrorHelper::not_supported("Dryer");
    }

    /**
     * @brief Get available drying presets
     *
     * Returns preset profiles for common filament materials.
     * Backends can override to provide hardware-specific presets.
     * Falls back to get_default_drying_presets() if not overridden.
     *
     * @return Vector of DryingPreset structs
     */
    [[nodiscard]] virtual std::vector<DryingPreset> get_drying_presets() const {
        return get_default_drying_presets();
    }

    // ========================================================================
    // Endless Spool Control
    // ========================================================================

    /**
     * @brief Get endless spool capabilities for this backend
     *
     * Returns information about whether endless spool is supported and
     * whether the configuration can be modified via the UI.
     *
     * @return Capabilities struct with supported/editable flags
     */
    [[nodiscard]] virtual helix::printer::EndlessSpoolCapabilities
    get_endless_spool_capabilities() const {
        return {false, false, ""}; // Default: not supported
    }

    /**
     * @brief Get endless spool configuration for all slots
     *
     * Returns the backup slot configuration for each slot in the system.
     * For Happy Hare, this translates the group-based configuration to
     * per-slot backup mappings.
     *
     * @return Vector of configs, one per slot
     */
    [[nodiscard]] virtual std::vector<helix::printer::EndlessSpoolConfig>
    get_endless_spool_config() const {
        return {}; // Default: empty
    }

    /**
     * @brief Set backup slot for endless spool
     *
     * Configures which slot will be used as a backup when the specified
     * slot runs out of filament. Pass -1 as backup_slot to disable backup.
     *
     * Not all backends support editing:
     * - AFC: Fully editable via SET_RUNOUT G-code
     * - Happy Hare: Read-only (configured via mmu_vars.cfg)
     *
     * @param slot_index Source slot
     * @param backup_slot Backup slot (-1 to disable)
     * @return AmsError with result
     */
    virtual AmsError set_endless_spool_backup(int slot_index, int backup_slot) {
        (void)slot_index;
        (void)backup_slot;
        return AmsErrorHelper::not_supported("Endless spool");
    }

    /**
     * @brief Reset all tool mappings to defaults
     *
     * Resets tool-to-slot mappings to their original/default configuration.
     * Default behavior is typically 1:1 mapping (T0→Slot0, T1→Slot1, etc.).
     *
     * @return AmsError with result
     */
    virtual AmsError reset_tool_mappings() {
        return AmsErrorHelper::not_supported("Reset tool mappings");
    }

    /**
     * @brief Reset all endless spool backup mappings
     *
     * Clears all endless spool backup slot configurations, setting each
     * slot's backup to -1 (no backup).
     *
     * @return AmsError with result
     */
    virtual AmsError reset_endless_spool() {
        return AmsErrorHelper::not_supported("Reset endless spool");
    }

    // ========================================================================
    // Tool Mapping Control
    // ========================================================================

    /**
     * @brief Get tool mapping capabilities for this backend
     *
     * Returns information about whether tool mapping is supported and
     * whether the configuration can be modified via the UI.
     *
     * @return Capabilities struct with supported/editable flags
     */
    [[nodiscard]] virtual helix::printer::ToolMappingCapabilities
    get_tool_mapping_capabilities() const {
        return {false, false, ""}; // Default: not supported
    }

    /**
     * @brief Get current tool-to-slot mapping
     *
     * Returns the mapping from tool number to slot index.
     * The vector index represents the tool number, and the value at that
     * index is the slot that tool maps to.
     *
     * @return Vector where index=tool, value=slot (empty if not supported)
     */
    [[nodiscard]] virtual std::vector<int> get_tool_mapping() const {
        return {}; // Default: empty
    }

    // ========================================================================
    // Device-Specific Actions
    // ========================================================================

    /**
     * @brief Get available device sections for this backend
     *
     * Sections group related actions (e.g., "Calibration", "Speed Settings").
     * UI renders sections in display_order.
     *
     * @return Vector of DeviceSection (empty if no device-specific features)
     */
    [[nodiscard]] virtual std::vector<helix::printer::DeviceSection> get_device_sections() const {
        return {};
    }

    /**
     * @brief Get available device actions
     *
     * Returns all device-specific actions. UI groups them by section ID.
     *
     * @return Vector of DeviceAction (empty if no device-specific features)
     */
    [[nodiscard]] virtual std::vector<helix::printer::DeviceAction> get_device_actions() const {
        return {};
    }

    /**
     * @brief Execute a device action
     *
     * @param action_id The action ID from get_device_actions()
     * @param value Optional value for toggles/sliders/dropdowns
     * @return AmsError indicating success/failure
     */
    virtual AmsError execute_device_action(const std::string& action_id,
                                           const std::any& value = {}) {
        (void)action_id;
        (void)value;
        return AmsErrorHelper::not_supported("Device actions");
    }

    // ========================================================================
    // Capability Queries
    // ========================================================================

    /**
     * @brief How this backend maps tool numbers to filament slots.
     *
     * Used by the preflight filament validator to decide whether the slicer's
     * Tx tool-change commands need to be rewritten before the print starts, or
     * whether the backend handles routing internally.
     *
     *  None            — base / default; no multi-tool routing (single-extruder,
     *                    no AMS attached)
     *  Native          — backend owns the T0..Tn → slot mapping internally
     *                    (Happy Hare, AFC, CFS, AD5X IFS, ToolChanger); helix
     *                    does NOT rewrite gcode
     *  GcodeRewrite    — helix must rewrite Tx commands in the gcode file because
     *                    the firmware has no internal tool-routing table (ACE)
     *  SnapmakerNative — backend emits firmware-native print_task_config gcode
     *                    (SET_PRINT_USED_EXTRUDERS / SET_PRINT_EXTRUDER_MAP) before
     *                    PRINT_START; no gcode-file rewrite (Snapmaker U1)
     */
    enum class RemapStrategy { None, Native, GcodeRewrite, SnapmakerNative };

    /**
     * @brief Get the tool-remapping strategy for this backend.
     *
     * Default: RemapStrategy::None (base class / no-AMS path).
     * Overridden per backend — see RemapStrategy doc above.
     */
    [[nodiscard]] virtual RemapStrategy get_remap_strategy() const {
        return RemapStrategy::None;
    }

    /**
     * @brief Clear the backend's persistent message/error queue.
     *
     * AFC keeps a persistent message queue that will not clear until
     * AFC_CLEAR_MESSAGE is sent; without it the error dialog re-fires immediately
     * because AFC keeps reporting ERROR (#497). Default is a no-op
     * (NOT_SUPPORTED); backends with such a queue override it.
     *
     * Callers do not need to ask whether a backend has a queue first — the
     * default is already a harmless no-op, which is why the former
     * supports_clear_message_queue() capability query was removed once
     * clear_fault() took over the dismiss path.
     *
     * @return AmsError indicating success/failure
     */
    virtual AmsError clear_message_queue() {
        return AmsErrorHelper::not_supported("Clear message queue");
    }

    /**
     * @brief Whether the idle filament-runout guidance modal should be suppressed.
     *
     * Some backends (Snapmaker U1) drive load/unload entirely on their own, so an
     * idle lane going empty — a hand-pull or a lane simply left unloaded — needs no
     * operator action and the runout-guidance modal is just noise. Backends that
     * require manual intervention keep the idle modal. Mid-print runout is a
     * separate path and is unaffected by this flag.
     *
     * @return true to suppress the idle runout modal for this backend
     */
    [[nodiscard]] virtual bool should_suppress_idle_runout_modal() const {
        return false;
    }

    /**
     * @brief Whether this backend is an AFC (Armored Turtle) system.
     *
     * Identity gate for AFC-specific UI sections (e.g. the unload-after-print
     * toggle in the device-operations overlay) that have no behavioral analogue
     * on other backends. Only AFC overrides this.
     *
     * @return true if this is an AFC backend
     */
    [[nodiscard]] virtual bool is_afc_system() const {
        return false;
    }

    /**
     * @brief Whether the backend exposes user-configurable eject parameters
     *        (distance + velocity) that the UI should surface as sliders.
     *
     * QIDI Box lets the user tune the per-lane eject distance and velocity; the
     * device-operations overlay renders the slider rows only for such backends.
     *
     * @return true if eject distance/velocity are configurable for this backend
     */
    [[nodiscard]] virtual bool supports_configurable_eject_params() const {
        return false;
    }

    /**
     * @brief Whether the UI may synthesise a prerequisite operation on the
     *        user's behalf to make a requested action succeed.
     *
     * When false, the UI issues exactly ONE command per user action and lets
     * the backend refuse if the machine is not in a state to accept it. When
     * true, the UI may chain — e.g. unload the loaded slot first, then enable
     * bypass once the unload completes.
     *
     * AFC and Happy Hare users have a console and expect the screen to pass
     * their commands straight through; a bypass toggle that quietly ejects the
     * filament in the toolhead is a command they never issued
     * (prestonbrown/helixscreen#1229). OEM backends (CFS, ACE, AD5X IFS,
     * Snapmaker, QIDI) have no console fallback, so the chaining is the only
     * way their users can reach the desired state and it stays enabled.
     *
     * @return true if the UI may issue an implicit prerequisite command
     */
    [[nodiscard]] virtual bool allows_implicit_chaining() const {
        return true;
    }

    /**
     * @brief Whether each slot has a physical tray/housing to draw in the AMS detail view.
     *
     * Selector/hub and box systems have a physical tray; tool changers give each
     * tool its own independent toolhead with no shared tray/housing, so the tray
     * graphic is hidden for them.
     *
     * @return true if a physical tray should be drawn
     */
    [[nodiscard]] virtual bool has_physical_tray() const {
        return true;
    }

    /**
     * @brief Whether the per-slot tool badge ("T0", "T1", ...) should be hidden.
     *
     * On tool changers the badge is redundant with the toolhead label shown below
     * each slot, so it is suppressed. Other backends show it.
     *
     * @return true to hide the per-slot tool badge
     */
    [[nodiscard]] virtual bool should_hide_slot_tool_badge() const {
        return false;
    }

    /**
     * @brief Whether the backend assigns one spool per tool (tool-changer model).
     *
     * Drives the auto-assign-active-spool-to-tool path and the runout-guidance
     * routing: tool changers have one extruder per slot with no shared hub, so a
     * runout cannot be auto-resolved by swapping spools. Default mirrors the
     * existing is_tool_changer(get_type()) classification so behavior is identical;
     * backends override only if their per-tool model diverges from their type.
     *
     * @return true if each tool owns its own spool assignment
     */
    [[nodiscard]] virtual bool supports_per_tool_spool_assignment() const {
        return is_tool_changer(get_type());
    }

    /**
     * @brief Whether the backend reports a continuous sync-feedback bias the UI can
     *        visualize (proportional buffer bias + fault tinting).
     *
     * Happy Hare reports printer.mmu.sync_feedback_bias; the value is meaningful
     * only when > -1.5 (the sentinel for "no bias data"). The buffer meter, path
     * canvas tinting, and clog-detection buffer page all gate on this. Backends
     * without a continuous bias signal return false (discrete mode).
     *
     * @param info Current system snapshot (carries sync_feedback_bias)
     * @return true if a proportional sync-feedback bias is available
     */
    [[nodiscard]] virtual bool
    supports_sync_feedback_visualization(const AmsSystemInfo& info) const {
        (void)info;
        return false;
    }

    /**
     * @brief Klipper object / expected-hardware name for this backend.
     *
     * Used when recording expected hardware during wizard setup so the hardware
     * validator can warn if the AMS disappears. Matches the firmware object name
     * (e.g. "AFC", "mmu", "toolchanger"). Empty string => not a tracked Klipper
     * object (e.g. REST-based systems). Default empty; backends override.
     *
     * @return Klipper object name, or "" if none
     */
    [[nodiscard]] virtual const char* get_klipper_object_name() const {
        return "";
    }

    /**
     * @brief Whether this backend must emit firmware-native config gcode BEFORE
     *        PRINT_START.
     *
     * Backends that answer true have PrintStartController call
     * build_preprint_gcode() and send the result (synchronously gating the print
     * start) before the print begins. Default false — every backend takes the
     * unchanged synchronous start path. Snapmaker U1 overrides it true: its
     * firmware errors if SET_PRINT_USED_EXTRUDERS / SET_PRINT_EXTRUDER_MAP arrive
     * mid-print, and the pre-send is always-on (even with no remap) to suppress a
     * spurious-feed runout. This is a backend capability, not a remap-strategy
     * proxy — the controller no longer compares get_remap_strategy().
     *
     * @return true if a pre-print send is required
     */
    [[nodiscard]] virtual bool requires_preprint_send() const {
        return false;
    }

    /**
     * @brief Firmware-native pre-print command sequence (e.g. print_task_config).
     *
     * Backends that answer requires_preprint_send() == true emit firmware-native
     * gcode (SET_PRINT_USED_EXTRUDERS / SET_PRINT_EXTRUDER_MAP) BEFORE PRINT_START.
     * The caller gates on requires_preprint_send() and sends the returned gcode.
     * Default returns "" (no native pre-print step); the Snapmaker backend
     * overrides it.
     *
     * @param tools_used Logical tools the gcode body uses
     * @param remap      Logical tool -> physical head, only for changed tools
     * @return Newline-joined gcode (no trailing newline), or "" when none
     */
    [[nodiscard]] virtual std::string build_preprint_gcode(const std::set<int>& tools_used,
                                                           const std::map<int, int>& remap) const {
        (void)tools_used;
        (void)remap;
        return "";
    }

    /**
     * @brief Check if backend automatically heats extruder before loading
     *
     * Some backends (like AFC) use material-specific temperatures from their
     * configuration (e.g., default_material_temps in AFC.cfg) to preheat the
     * extruder before loading filament. This eliminates the need for the UI
     * to manage preheating.
     *
     * @return true if backend handles preheat automatically, false if UI should manage it
     */
    [[nodiscard]] virtual bool supports_auto_heat_on_load() const {
        return false;
    }

    /**
     * @brief Whether this backend persists spool assignments via firmware
     *
     * Backends like Happy Hare (MMU_GATE_MAP/SPOOLID) and AFC (SET_SPOOL_ID)
     * store spool-to-slot mappings in firmware gcode. For these, ToolState
     * mirrors firmware state but doesn't need to persist separately.
     *
     * Backends without firmware persistence (tool changers, ACE) rely on
     * ToolState's own persistence (Moonraker DB + local JSON).
     *
     * @return true if firmware handles spool persistence, false if ToolState should
     */
    [[nodiscard]] virtual bool has_firmware_spool_persistence() const {
        return false;
    }

    /**
     * @brief Whether this backend unloads the toolhead automatically after a print
     *
     * Some filament systems (e.g. AD5X IFS) retract filament out of the extruder
     * at end-of-print by default, so an empty toolhead between jobs is the normal
     * resting state. For these backends, a runout-sensor reading "no filament" at
     * print-start is expected, not a warning condition, and the pre-print
     * filament-missing modal should be suppressed.
     *
     * @return true if the backend is expected to leave the toolhead empty between
     *         prints, false otherwise
     */
    [[nodiscard]] virtual bool auto_unloads_after_print() const {
        return false;
    }

    /**
     * @brief Whether this backend feeds filament to the nozzle as part of resume
     *
     * True when this backend feeds filament to the nozzle as part of resume (e.g.
     * Snapmaker U1: Resume runs AUTO_FEEDING then RESUME). Drives the runout dialog:
     * such backends present Resume as the primary action and demote manual
     * Load/Unload/Purge to a secondary row, because Resume alone recovers a runout.
     * Backends without this (basic runout sensors, most MMUs) need a manual Load
     * before resume, so they keep Load prominent. Default false (conservative).
     */
    [[nodiscard]] virtual bool recovers_filament_on_resume() const {
        return false;
    }

    /**
     * @brief Check if backend provides per-unit environment sensors (temp/humidity)
     *
     * CFS units have built-in temperature and humidity sensors. Other backends
     * (Happy Hare, ACE, AFC, Tool Changers) do not.
     *
     * @return true if backend provides environment sensor data per unit
     */
    [[nodiscard]] virtual bool has_environment_sensors() const {
        return false;
    }

    /**
     * @brief Get the list of material strings this backend's firmware will accept.
     *
     * Returns std::nullopt if the backend accepts any material string
     * (default for AFC, Happy Hare, ACE, CFS — firmware treats material as
     * a free-form label).
     *
     * Returns a non-empty vector when firmware validates the string against
     * a fixed list (e.g., AD5X IFS rejects anything outside its 7-item set).
     * Callers should filter dropdowns and normalize outgoing values to this
     * list. An empty vector is treated the same as nullopt (no restriction).
     */
    [[nodiscard]] virtual std::optional<std::vector<std::string>> get_supported_materials() const {
        return std::nullopt;
    }

    /**
     * @brief Firmware-specific name aliases layered between exact match and
     *        compat_group fallback inside normalize_material().
     *
     * Each entry maps an alternate spelling (case-insensitive) to a whitelist
     * entry. Use when the shared filament database groups variants differently
     * than the firmware does. Example: AD5X IFS treats SILK as distinct from
     * PLA, but "Silk PLA" has compat_group "PLA" in the shared DB, so without
     * an alias it would normalize to "PLA" instead of "SILK".
     *
     * Returned pairs: {alias, whitelist_target}. Targets should themselves be
     * present in get_supported_materials(); if not, normalize_material() will
     * still return them (caller beware).
     *
     * @return Vector of (alias -> target) pairs. Empty by default.
     */
    [[nodiscard]] virtual std::vector<std::pair<std::string, std::string>>
    get_material_aliases() const {
        return {};
    }

    /**
     * @brief Normalize an arbitrary material string to one the firmware will accept.
     *
     * Called before sending material to firmware. Pipeline:
     *   1. If get_supported_materials() returns nullopt/empty -> return input unchanged.
     *   2. Case-insensitive exact match against whitelist -> return whitelist entry.
     *   3. Case-insensitive match against get_material_aliases() -> return mapped target.
     *   4. Look up input via filament::find_material() -> get compat_group -> find
     *      first whitelist entry whose compat_group matches -> return it.
     *   5. Fallback: return first whitelist entry (safest default, usually PLA).
     *
     * Backends that need more than alias mapping can override this method.
     *
     * @param material Input material string (may be empty)
     * @return Normalized string safe to send to firmware
     */
    [[nodiscard]] virtual std::string normalize_material(const std::string& material) const;

    // ========================================================================
    // Discovery Configuration (Optional - default implementations are no-ops)
    // ========================================================================

    /**
     * @brief Set discovered lane and hub names from PrinterCapabilities
     *
     * Called before start() to provide lane names discovered from printer.objects.list.
     * Only AFC backend uses this - other backends ignore it.
     *
     * @param lane_names Lane names from PrinterCapabilities::get_afc_lane_names()
     * @param hub_names Hub names from PrinterCapabilities::get_afc_hub_names()
     */
    virtual void set_discovered_lanes(const std::vector<std::string>& lane_names,
                                      const std::vector<std::string>& hub_names) {
        (void)lane_names;
        (void)hub_names;
    }

    /**
     * @brief Set discovered tool names from PrinterCapabilities
     *
     * Called before start() to provide tool names discovered from printer.objects.list.
     * Only tool changer backend uses this - other backends ignore it.
     *
     * @param tool_names Tool names from PrinterCapabilities::get_tool_names()
     */
    virtual void set_discovered_tools(std::vector<std::string> tool_names) {
        (void)tool_names;
    }

    /**
     * @brief Set filament sensor names from PrinterCapabilities
     *
     * Called before start() to provide filament sensor names from printer.objects.list.
     * AFC backend uses this to detect hardware vs virtual bypass sensor.
     *
     * @param sensor_names Sensor names (e.g., "filament_switch_sensor virtual_bypass")
     */
    virtual void set_discovered_sensors(const std::vector<std::string>& sensor_names) {
        (void)sensor_names;
    }

    // ========================================================================
    // Mock Support
    // ========================================================================

    /**
     * @brief Set callback for gcode response injection
     *
     * Used by mock backends to simulate Klipper gcode responses.
     * Default implementation is a no-op for real backends.
     *
     * @param callback Function that receives gcode response lines
     */
    virtual void set_gcode_response_callback(std::function<void(const std::string&)>) {}

    // ========================================================================
    // Sensor Ownership
    // ========================================================================

    /**
     * @brief Whether the backend of @p type owns the filament sensor @p bare_name.
     *
     * Replaces the former switch(AmsType) in PrinterHardware::is_ams_sensor that
     * mapped each backend's conventionally-named (keyword-free) filament sensors.
     * Each backend that owns named sensors implements a static
     * owns_filament_sensor(bare, discovery); this dispatcher routes by type so a
     * new backend declares its patterns in its own translation unit instead of
     * editing printer_hardware.cpp (#1054).
     *
     * Static rather than virtual because the callers (hardware validation,
     * sensor settings, wizard sensor select) run during discovery — before any
     * AmsBackend instance exists, and create(AmsType) returns nullptr for real
     * backends without a live API/client. The recognition is pure name/discovery
     * pattern matching with no per-instance state, so it needs no backend object.
     *
     * Keyword-bearing sensor names (mmu_*, afc_*, lane*, gate*, ...) are handled
     * by the type-independent substring path in PrinterHardware and are NOT this
     * method's concern.
     *
     * @param type      Detected AMS/MMU backend type
     * @param bare_name Sensor name with the Klipper object-type prefix stripped
     * @param discovery Current printer discovery (provides AFC lane/buffer names)
     * @return true if the backend of @p type claims this sensor name
     */
    static bool sensor_belongs_to_backend(AmsType type, const std::string& bare_name,
                                          const helix::PrinterDiscovery& discovery);

    // ========================================================================
    // Factory Method
    // ========================================================================

    /**
     * @brief Create appropriate backend for detected AMS type (mock only)
     *
     * Factory method that creates a mock backend for testing.
     * For real backends, use the overload that accepts MoonrakerAPI and MoonrakerClient.
     *
     * In mock mode (RuntimeConfig::should_mock_ams()), returns AmsBackendMock.
     *
     * @param detected_type The detected AMS type from printer discovery
     * @return Unique pointer to backend instance, or nullptr if type is NONE
     * @deprecated Use create(AmsType, MoonrakerAPI*, helix::MoonrakerClient*) for real backends
     */
    static std::unique_ptr<AmsBackend> create(AmsType detected_type);

    /**
     * @brief Create appropriate backend for detected AMS type with dependencies
     *
     * Factory method that creates the correct backend implementation:
     * - HAPPY_HARE: AmsBackendHappyHare (requires api and client)
     * - AFC: AmsBackendAfc (requires api and client)
     * - NONE: nullptr (no AMS detected)
     *
     * In mock mode (RuntimeConfig::should_mock_ams()), returns AmsBackendMock.
     *
     * @param detected_type The detected AMS type from printer discovery
     * @param api Pointer to MoonrakerAPI for sending commands
     * @param client Pointer to helix::MoonrakerClient for subscriptions
     * @return Unique pointer to backend instance, or nullptr if type is NONE
     */
    static std::unique_ptr<AmsBackend> create(AmsType detected_type, MoonrakerAPI* api,
                                              helix::MoonrakerClient* client);

    /**
     * @brief Create mock backend for testing
     *
     * Creates a mock backend regardless of actual printer state.
     * Used when --test flag is passed or for development.
     *
     * @param slot_count Number of simulated slots (default 4)
     * @return Unique pointer to mock backend instance
     */
    static std::unique_ptr<AmsBackend> create_mock(int slot_count = 4);
};
