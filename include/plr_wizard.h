// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The plugin-driven (GENERIC) power-loss resume: the plugin's replies as the
// app reads them, and the pure decisions the resume wizard steps through. No
// LVGL, no network. Wire contract: server.helix.plr.* in
// docs/devel/POWER_LOSS_RECOVERY.md § "Generic (HelixPrint plugin)".

#include "json_fwd.h"
#include "plr_backend.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace helix {

/// A bed that has cooled more than this below its target since the outage may
/// have let the part go: resuming then needs an explicit accept_cold_bed.
inline constexpr float kPlrColdBedDiffC = 10.0f;
/// Within this of its target the bed counts as at temperature.
inline constexpr float kPlrBedOkDiffC = 5.0f;

/// The resume wizard's steps. Offer is the wait for the plan (the offer modal
/// already ran); Failed and Aborted are reachable from every live step.
enum class PlrWizardStep { Offer, Checks, Preparing, ZStep, Committing, Done, Failed, Aborted };

/// What the user confirmed; each maps to one wire name (plr_confirmation_name).
enum class PlrConfirmation {
    PartAttached,
    NozzleClear,
    ZConfirmed,
    ZJogged,
    AcceptNoMesh,
    AcceptColdBed,
    AcceptNextLayer
};

enum class PlrBedState { Ok, NeedsWait, ColdWarn };

/// The plan as the wizard decides on it.
struct PlrPlanView {
    int tier = 0;
    std::vector<std::string> warnings;
    float bed_target = 0.0f;
    float bed_current = 0.0f;
    std::string plan_id;
    PlrPluginBackend backend = PlrPluginBackend::None;
    /// The plugin's own `requires`; commit needs these as well as the app's.
    std::vector<PlrConfirmation> plugin_requires;
    /// The plugin requires a confirmation this app does not know, so no set
    /// of answers here can satisfy its commit.
    bool unsatisfiable = false;
};

/// One evaluation of the step machine. Checks and ZStep advance when `given`
/// covers that step's confirmations, so callers evaluate those steps on the
/// user's tap; the RPC steps advance on rpc_ok / rpc_failed.
struct PlrWizardInputs {
    PlrWizardStep step = PlrWizardStep::Offer;
    bool rpc_ok = false;
    bool rpc_failed = false;
    bool connected = true;
    bool user_abort = false;
    const PlrPlanView* plan = nullptr;
    std::vector<PlrConfirmation> given;
};

/// Ok when target is 0 or current is within @p max_diff_c of it, NeedsWait
/// within kPlrColdBedDiffC, ColdWarn beyond.
PlrBedState plr_bed_decision(float target_c, float current_c, float max_diff_c);

/// Everything commit needs: tier 1 part_attached, nozzle_clear, z_jogged; tier 2
/// part_attached, z_confirmed; plus accept_no_mesh for a `mesh_unavailable`
/// warning, accept_next_layer for a `layer_start` warning, accept_cold_bed for
/// ColdWarn, and the plugin's own `requires`.
/// Any other tier needs nothing because it can never commit.
std::vector<PlrConfirmation> plr_required_confirmations(const PlrPlanView& plan, PlrBedState bed);

/// The subset of plr_required_confirmations() asked before the printer moves:
/// everything except the Z confirmation.
std::vector<PlrConfirmation> plr_check_confirmations(const PlrPlanView& plan, PlrBedState bed);

/// Tier 1 or 2, satisfiable, and every required confirmation given.
bool plr_commit_allowed(const PlrPlanView& plan, const std::vector<PlrConfirmation>& given,
                        PlrBedState bed);

/// The next step. Never commits on its own: Committing is reached only from
/// ZStep with the confirmations given, and a lost connection fails the flow.
PlrWizardStep plr_next_wizard_step(const PlrWizardInputs& in);

/// The bed state of a plan, from its own bed reading.
PlrBedState plr_plan_bed_state(const PlrPlanView& plan);

/// What the Z step tells the user about the nozzle. plr_wizard_overlay.xml
/// binds these values.
enum class PlrZStepKind {
    /// Tier 1: the user lowers the nozzle onto the top of the print.
    Jog = 0,
    /// Prepare lifted the nozzle and homed X and Y; commit lowers it again.
    ConfirmLifted = 1,
    /// Prepare moved nothing: the nozzle is where the power cut left it.
    ConfirmInPlace = 2,
};

/// True when the plugin's prepare moves the toolhead: the generic backend
/// lifts the nozzle and homes X and Y, the QIDI backend only heats.
bool plr_prepare_moves_head(const PlrPlanView& plan);

PlrZStepKind plr_z_step_kind(const PlrPlanView& plan);

const char* plr_confirmation_name(PlrConfirmation c);
std::optional<PlrConfirmation> plr_parse_confirmation(const std::string& name);

/// "generic" / "qidi_print_state"; anything else is None.
PlrPluginBackend plr_parse_plugin_backend(const std::string& name);

/// server.helix.plr.status `snapshot`.
struct PlrSnapshotInfo {
    std::string job_id;
    std::string filename;
    std::optional<int> layer;
    double z = 0.0;
    double saved_at = 0.0;
    /// Seconds since the snapshot was saved; negative when not reported.
    double age_s = -1.0;
};

/// server.helix.plr.status, as read from its `result`.
struct PlrPluginStatus {
    bool saving = false;
    int tier = 0;
    PlrPluginBackend backend = PlrPluginBackend::None;
    std::optional<PlrSnapshotInfo> snapshot;
    bool offer_available = false;
    std::vector<std::string> blocked_by;
};

/// Never throws; a malformed field reads as its default, which offers nothing.
PlrPluginStatus plr_parse_plugin_status(const nlohmann::json& result);

/// Why the plugin withholds the offer, for the log, for one `offer.blocked_by`
/// code; a code this app does not know comes back as itself.
std::string plr_blocked_reason(const std::string& code);

/// server.helix.plr.plan, as read from its `result`.
struct PlrPlanReply {
    /// The scan is still running; the plan follows with notify_plr_plan_ready.
    bool pending = false;
    std::string plan_id;
    std::string filename;
    std::string method;
    std::int64_t resume_offset = 0;
    std::optional<int> layer;
    double z_gcode = 0.0;
    double z_physical = 0.0;
    double retrace_estimate_mm = 0.0;
    float bed_target = 0.0f;
    float bed_current = 0.0f;
    std::vector<std::string> restores;
    std::vector<std::string> warnings;
    std::vector<PlrConfirmation> requires_confirmations;
    std::vector<std::string> unknown_requires;
    /// Tier, when the plan carries one; the status tier applies otherwise.
    std::optional<int> tier;
};

/// nullopt when the reply is neither pending nor a plan with a plan_id.
std::optional<PlrPlanReply> plr_parse_plan_reply(const nlohmann::json& result);

/// The wizard's view of a plan. @p status_tier applies when the plan has none.
PlrPlanView plr_plan_view(const PlrPlanReply& plan, int status_tier, PlrPluginBackend backend);

/// helix:plr_progress, as Moonraker delivers it (notify_plr_progress).
struct PlrProgressEvent {
    std::string plan_id;
    std::string step;
    std::string state;
};

/// Reads the whole notification message; nullopt when it carries no payload.
std::optional<PlrProgressEvent> plr_parse_progress_event(const nlohmann::json& msg);

/// helix:plr_plan_ready: the plan it finished, or why the scan failed.
struct PlrPlanReady {
    std::string plan_id;
    /// Non-empty when the background scan failed; no plan follows.
    std::string error;
};

PlrPlanReady plr_parse_plan_ready(const nlohmann::json& msg);

/// The commit request: every given confirmation, and the Z reference the tier
/// resumes from (tier 1 the part top the user jogged to, else the trusted frame).
nlohmann::json plr_commit_params(const std::string& plan_id,
                                 const std::vector<PlrConfirmation>& given, int tier);

} // namespace helix
