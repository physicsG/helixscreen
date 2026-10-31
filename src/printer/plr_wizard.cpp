// SPDX-License-Identifier: GPL-3.0-or-later
#include "plr_wizard.h"

#include "json_utils.h"

#include <algorithm>
#include <utility>

#include "hv/json.hpp"

namespace helix {

namespace {

constexpr const char* kMeshUnavailableWarning = "mesh_unavailable";
constexpr const char* kLayerStartWarning = "layer_start";

struct ConfirmationName {
    PlrConfirmation value;
    const char* name;
};

constexpr ConfirmationName kConfirmationNames[] = {
    {PlrConfirmation::PartAttached, "part_attached"},
    {PlrConfirmation::NozzleClear, "nozzle_clear"},
    {PlrConfirmation::ZConfirmed, "z_confirmed"},
    {PlrConfirmation::ZJogged, "z_jogged"},
    {PlrConfirmation::AcceptNoMesh, "accept_no_mesh"},
    {PlrConfirmation::AcceptColdBed, "accept_cold_bed"},
    {PlrConfirmation::AcceptNextLayer, "accept_next_layer"},
};

bool contains(const std::vector<PlrConfirmation>& set, PlrConfirmation c) {
    return std::find(set.begin(), set.end(), c) != set.end();
}

void add_unique(std::vector<PlrConfirmation>& set, PlrConfirmation c) {
    if (!contains(set, c)) {
        set.push_back(c);
    }
}

bool covers(const std::vector<PlrConfirmation>& given, const std::vector<PlrConfirmation>& need) {
    return std::all_of(need.begin(), need.end(),
                       [&given](PlrConfirmation c) { return contains(given, c); });
}

bool is_z_confirmation(PlrConfirmation c) {
    return c == PlrConfirmation::ZConfirmed || c == PlrConfirmation::ZJogged;
}

bool resumable_tier(int tier) {
    return tier == 1 || tier == 2;
}

std::vector<std::string> string_array(const nlohmann::json& obj, const char* key) {
    std::vector<std::string> out;
    const nlohmann::json* arr = json_util::find_member(obj, key);
    if (arr == nullptr || !arr->is_array()) {
        return out;
    }
    for (const auto& item : *arr) {
        if (item.is_string()) {
            out.push_back(item.get<std::string>());
        }
    }
    return out;
}

std::optional<int> optional_int(const nlohmann::json& obj, const char* key) {
    const nlohmann::json* v = json_util::find_member(obj, key);
    if (v == nullptr || !v->is_number()) {
        return std::nullopt;
    }
    return json_util::as_int(*v);
}

} // namespace

PlrBedState plr_bed_decision(float target_c, float current_c, float max_diff_c) {
    if (target_c <= 0.0f) {
        return PlrBedState::Ok;
    }
    const float below = target_c - current_c;
    if (below <= max_diff_c) {
        return PlrBedState::Ok;
    }
    if (below <= kPlrColdBedDiffC) {
        return PlrBedState::NeedsWait;
    }
    return PlrBedState::ColdWarn;
}

PlrBedState plr_plan_bed_state(const PlrPlanView& plan) {
    return plr_bed_decision(plan.bed_target, plan.bed_current, kPlrBedOkDiffC);
}

bool plr_prepare_moves_head(const PlrPlanView& plan) {
    return plan.backend == PlrPluginBackend::Generic;
}

PlrZStepKind plr_z_step_kind(const PlrPlanView& plan) {
    if (plan.tier == 1) {
        return PlrZStepKind::Jog;
    }
    return plr_prepare_moves_head(plan) ? PlrZStepKind::ConfirmLifted
                                        : PlrZStepKind::ConfirmInPlace;
}

std::vector<PlrConfirmation> plr_required_confirmations(const PlrPlanView& plan, PlrBedState bed) {
    std::vector<PlrConfirmation> need;
    if (plan.tier == 1) {
        need = {PlrConfirmation::PartAttached, PlrConfirmation::NozzleClear,
                PlrConfirmation::ZJogged};
    } else if (plan.tier == 2) {
        need = {PlrConfirmation::PartAttached, PlrConfirmation::ZConfirmed};
    } else {
        return need;
    }
    if (std::find(plan.warnings.begin(), plan.warnings.end(), kMeshUnavailableWarning) !=
        plan.warnings.end()) {
        add_unique(need, PlrConfirmation::AcceptNoMesh);
    }
    if (std::find(plan.warnings.begin(), plan.warnings.end(), kLayerStartWarning) !=
        plan.warnings.end()) {
        add_unique(need, PlrConfirmation::AcceptNextLayer);
    }
    if (bed == PlrBedState::ColdWarn) {
        add_unique(need, PlrConfirmation::AcceptColdBed);
    }
    for (PlrConfirmation c : plan.plugin_requires) {
        add_unique(need, c);
    }
    return need;
}

std::vector<PlrConfirmation> plr_check_confirmations(const PlrPlanView& plan, PlrBedState bed) {
    std::vector<PlrConfirmation> need = plr_required_confirmations(plan, bed);
    need.erase(std::remove_if(need.begin(), need.end(), is_z_confirmation), need.end());
    return need;
}

bool plr_commit_allowed(const PlrPlanView& plan, const std::vector<PlrConfirmation>& given,
                        PlrBedState bed) {
    if (!resumable_tier(plan.tier) || plan.unsatisfiable || plan.plan_id.empty()) {
        return false;
    }
    return covers(given, plr_required_confirmations(plan, bed));
}

PlrWizardStep plr_next_wizard_step(const PlrWizardInputs& in) {
    switch (in.step) {
    case PlrWizardStep::Done:
    case PlrWizardStep::Failed:
    case PlrWizardStep::Aborted:
        return in.step;
    default:
        break;
    }
    // Each live step is one request the plugin is acting on, or a question
    // whose answer a commit will rely on; neither survives the link.
    if (!in.connected) {
        return PlrWizardStep::Failed;
    }
    if (in.user_abort) {
        return PlrWizardStep::Aborted;
    }
    if (in.step != PlrWizardStep::Offer && in.plan == nullptr) {
        return PlrWizardStep::Failed;
    }

    switch (in.step) {
    case PlrWizardStep::Offer:
        if (in.rpc_failed) {
            return PlrWizardStep::Failed;
        }
        if (in.rpc_ok && in.plan != nullptr) {
            return in.plan->unsatisfiable || !resumable_tier(in.plan->tier) ? PlrWizardStep::Failed
                                                                            : PlrWizardStep::Checks;
        }
        return PlrWizardStep::Offer;
    case PlrWizardStep::Checks:
        return covers(in.given, plr_check_confirmations(*in.plan, plr_plan_bed_state(*in.plan)))
                   ? PlrWizardStep::Preparing
                   : PlrWizardStep::Checks;
    case PlrWizardStep::Preparing:
        if (in.rpc_failed) {
            return PlrWizardStep::Failed;
        }
        return in.rpc_ok ? PlrWizardStep::ZStep : PlrWizardStep::Preparing;
    case PlrWizardStep::ZStep:
        return plr_commit_allowed(*in.plan, in.given, plr_plan_bed_state(*in.plan))
                   ? PlrWizardStep::Committing
                   : PlrWizardStep::ZStep;
    case PlrWizardStep::Committing:
        if (in.rpc_failed) {
            return PlrWizardStep::Failed;
        }
        return in.rpc_ok ? PlrWizardStep::Done : PlrWizardStep::Committing;
    default:
        return in.step;
    }
}

const char* plr_confirmation_name(PlrConfirmation c) {
    for (const auto& entry : kConfirmationNames) {
        if (entry.value == c) {
            return entry.name;
        }
    }
    return "";
}

std::optional<PlrConfirmation> plr_parse_confirmation(const std::string& name) {
    for (const auto& entry : kConfirmationNames) {
        if (name == entry.name) {
            return entry.value;
        }
    }
    return std::nullopt;
}

PlrPluginBackend plr_parse_plugin_backend(const std::string& name) {
    if (name == "generic") {
        return PlrPluginBackend::Generic;
    }
    if (name == "qidi_print_state") {
        return PlrPluginBackend::QidiPrintState;
    }
    return PlrPluginBackend::None;
}

std::string plr_blocked_reason(const std::string& code) {
    static constexpr std::pair<const char*, const char*> kReasons[] = {
        {"not_interrupted", "the last print was not interrupted"},
        {"no_snapshot", "no snapshot of the interrupted print"},
        {"file_changed", "the print file changed since the snapshot"},
        {"too_early", "the print had barely started"},
        {"not_supported", "this printer cannot resume"},
        {"klippy_not_ready", "Klipper is not ready"},
        {"no_lift_room", "not enough Z headroom to lift the nozzle clear of the print"},
    };
    for (const auto& [name, reason] : kReasons) {
        if (code == name) {
            return reason;
        }
    }
    return code;
}

PlrPluginStatus plr_parse_plugin_status(const nlohmann::json& result) {
    PlrPluginStatus status;
    if (!result.is_object()) {
        return status;
    }
    status.saving = json_util::safe_bool(result, "saving", false);
    status.tier = json_util::safe_int(result, "tier", 0);
    status.backend = plr_parse_plugin_backend(json_util::safe_string(result, "backend", ""));
    const nlohmann::json* snap = json_util::find_member(result, "snapshot");
    if (snap != nullptr && snap->is_object()) {
        PlrSnapshotInfo info;
        info.job_id = json_util::safe_string(*snap, "job_id", "");
        info.filename = json_util::safe_string(*snap, "filename", "");
        info.layer = optional_int(*snap, "layer");
        info.z = json_util::safe_double(*snap, "z", 0.0);
        info.saved_at = json_util::safe_double(*snap, "saved_at", 0.0);
        info.age_s = json_util::safe_double(*snap, "age_s", -1.0);
        status.snapshot = std::move(info);
    }
    const nlohmann::json* offer = json_util::find_member(result, "offer");
    if (offer != nullptr && offer->is_object()) {
        status.offer_available = json_util::safe_bool(*offer, "available", false);
        status.blocked_by = string_array(*offer, "blocked_by");
    }
    // An offer with nothing to resume from is no offer.
    if (!status.snapshot) {
        status.offer_available = false;
    }
    return status;
}

std::optional<PlrPlanReply> plr_parse_plan_reply(const nlohmann::json& result) {
    if (!result.is_object()) {
        return std::nullopt;
    }
    PlrPlanReply plan;
    if (json_util::safe_bool(result, "pending", false)) {
        plan.pending = true;
        return plan;
    }
    plan.plan_id = json_util::safe_string(result, "plan_id", "");
    if (plan.plan_id.empty()) {
        return std::nullopt;
    }
    plan.filename = json_util::safe_string(result, "filename", "");
    plan.method = json_util::safe_string(result, "method", "");
    plan.resume_offset = json_util::safe_int64(result, "resume_offset", 0);
    plan.layer = optional_int(result, "layer");
    plan.z_gcode = json_util::safe_double(result, "z_gcode", 0.0);
    plan.z_physical = json_util::safe_double(result, "z_physical", 0.0);
    plan.retrace_estimate_mm = json_util::safe_double(result, "retrace_estimate_mm", 0.0);
    const nlohmann::json* bed = json_util::find_member(result, "bed");
    if (bed != nullptr && bed->is_object()) {
        plan.bed_target = json_util::safe_float(*bed, "target", 0.0f);
        plan.bed_current = json_util::safe_float(*bed, "current", 0.0f);
    }
    plan.restores = string_array(result, "restores");
    plan.warnings = string_array(result, "warnings");
    for (const std::string& name : string_array(result, "requires")) {
        if (auto c = plr_parse_confirmation(name)) {
            plan.requires_confirmations.push_back(*c);
        } else {
            plan.unknown_requires.push_back(name);
        }
    }
    plan.tier = optional_int(result, "tier");
    return plan;
}

PlrPlanView plr_plan_view(const PlrPlanReply& plan, int status_tier, PlrPluginBackend backend) {
    PlrPlanView view;
    view.tier = plan.tier.value_or(status_tier);
    view.warnings = plan.warnings;
    view.bed_target = plan.bed_target;
    view.bed_current = plan.bed_current;
    view.plan_id = plan.plan_id;
    view.backend = backend;
    view.plugin_requires = plan.requires_confirmations;
    view.unsatisfiable = !plan.unknown_requires.empty();
    return view;
}

std::optional<PlrProgressEvent> plr_parse_progress_event(const nlohmann::json& msg) {
    const nlohmann::json* payload = json_util::notification_payload(msg);
    if (payload == nullptr) {
        return std::nullopt;
    }
    PlrProgressEvent event;
    event.plan_id = json_util::safe_string(*payload, "plan_id", "");
    event.step = json_util::safe_string(*payload, "step", "");
    event.state = json_util::safe_string(*payload, "state", "");
    return event;
}

PlrPlanReady plr_parse_plan_ready(const nlohmann::json& msg) {
    PlrPlanReady ready;
    const nlohmann::json* payload = json_util::notification_payload(msg);
    if (payload != nullptr) {
        ready.plan_id = json_util::safe_string(*payload, "plan_id", "");
        ready.error = json_util::safe_string(*payload, "error", "");
    }
    return ready;
}

nlohmann::json plr_commit_params(const std::string& plan_id,
                                 const std::vector<PlrConfirmation>& given, int tier) {
    nlohmann::json confirmations = nlohmann::json::object();
    for (PlrConfirmation c : given) {
        confirmations[plr_confirmation_name(c)] = true;
    }
    return nlohmann::json{{"plan_id", plan_id},
                          {"confirmations", confirmations},
                          {"z_reference", tier == 1 ? "part_top" : "trusted"}};
}

} // namespace helix
