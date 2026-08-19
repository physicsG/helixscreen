// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "scripted_u1.h"

#include "app_globals.h"
#include "printer_state.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace helix::wasm {
namespace {

constexpr int NUM_HEADS = 4;

/// How long the Heat step takes, split into three visible ramp frames.
///
/// A real U1 takes tens of seconds to come up to temperature; nobody wants to
/// sit through that to look at a screen. Three seconds is long enough that the
/// step bar's live "nnn / nnn°C" readout is legibly counting up, and short
/// enough that a full load is over in about eight.
constexpr int HEAT_MS = 3000;

/// Working nozzle temperature for the seeded PETG machine. Also what the head
/// is seeded AT, so the sidebar's own preheat gate is already satisfied and a
/// load dispatches straight into the firmware sequence.
constexpr double LOAD_TEMP = 250.0;

/// The span HEAT_MS is quoted for (roughly ambient to working temperature).
/// A shorter ramp takes proportionally less, so a swap whose second half asks
/// for a temperature the first half already reached does not dwell for nothing.
constexpr double FULL_HEAT_SPAN = 220.0;
/// Floor for a heat dwell, so the step is still legible when there is no ramp.
constexpr uint32_t MIN_HEAT_DWELL_MS = 350;

/// Which ACE feeds which head in the seeded machine, -1 = stock feeder.
/// Heads 0 and 1 are ACE-fed, 2 and 3 are on their own spools, so one screen
/// shows both kinds of position — the same split the captured fixture has, at
/// two ACEs instead of one.
constexpr int SEED_HEAD_ACE[NUM_HEADS] = {0, 1, -1, -1};
constexpr int SEED_HEAD_BAY[NUM_HEADS] = {0, 2, -1, -1};

nlohmann::json make_slot(int index, const char* material, const char* brand, const char* color,
                         const char* status) {
    return {{"index", index},
            {"material", material},
            {"brand", brand},
            {"sku", ""},
            {"subtype", ""},
            {"rfid", 0},
            {"status", status},
            // The capture carries colour as a 3-int array on a slot (and as a hex
            // string on head_source). Both shapes are real; keep them straight.
            {"color",
             {std::strtol(std::string(color).substr(0, 2).c_str(), nullptr, 16),
              std::strtol(std::string(color).substr(2, 2).c_str(), nullptr, 16),
              std::strtol(std::string(color).substr(4, 2).c_str(), nullptr, 16)}}};
}

nlohmann::json make_ace_unit(int idx, int temp, int humidity, const nlohmann::json& slots,
                             const nlohmann::json& gate_status) {
    return {{"idx", idx},
            {"connected", true},
            {"status", "ready"},
            {"protocol", "v2"},
            {"temp", temp},
            {"humidity", humidity},
            {"feed_assist", 0},
            {"auto_dry_running", false},
            {"auto_dry",
             {{"enabled", false},
              {"master", -1},
              {"temp", 50},
              {"rh_start", 45.0},
              {"rh_end", 35.0},
              {"add_time", 60}}},
            {"dryer_status",
             {{"status", "stop"}, {"target_temp", 0}, {"duration", 0}, {"remain_time", 0}}},
            {"gate_status", gate_status},
            {"slots", slots}};
}

} // namespace

// ============================================================================
// Construction / seed
// ============================================================================

ScriptedU1::ScriptedU1() : AmsBackendMultiAce(nullptr, nullptr) {
    // No client and no API: start() would refuse (it needs both to subscribe),
    // and there is nothing to subscribe TO. Marking the backend running is
    // exactly what the multiACE unit tests do for the same reason.
    running_.store(true);
}

ScriptedU1::~ScriptedU1() {
    abort_script();
    if (heater_timer_) {
        lv_timer_delete(heater_timer_);
        heater_timer_ = nullptr;
    }
}

void ScriptedU1::begin() {
    nlohmann::json left = nlohmann::json::object();
    nlohmann::json right = nlohmann::json::object();
    for (int h = 0; h < NUM_HEADS; ++h) {
        nlohmann::json ch = {{"channel_state", "wait_insert"},
                             {"channel_error", "ok"},
                             {"channel_error_state", "none"},
                             {"channel_action_state", "none"},
                             {"module_exist", true},
                             {"disable_auto", false},
                             {"filament_detected", true},
                             {"filament_at_extruder", false},
                             {"filament_in_ace", SEED_HEAD_ACE[h] >= 0},
                             {"filament_in_toolhead", false}};
        (h < 2 ? left : right)[fmt::format("extruder{}", h)] = std::move(ch);
    }

    nlohmann::json head_ace = nlohmann::json::object();
    nlohmann::json head_feeder = nlohmann::json::object();
    nlohmann::json head_manual = nlohmann::json::object();
    nlohmann::json head_source = nlohmann::json::object();
    for (int h = 0; h < NUM_HEADS; ++h) {
        const std::string k = std::to_string(h);
        const bool ace_fed = SEED_HEAD_ACE[h] >= 0;
        head_ace[k] = ace_fed ? SEED_HEAD_ACE[h] : 0;
        head_feeder[k] = !ace_fed; // feeder=false is what makes a head ACE-fed
        head_manual[k] = false;
        head_source[k] = nullptr;
    }

    nlohmann::json ace1_slots =
        nlohmann::json::array({make_slot(0, "PETG", "Kingroon", "83AFFF", "ready"),
                               make_slot(1, "PETG", "Kingroon", "8FA7C8", "ready"),
                               make_slot(2, "PETG", "Generic", "632C2C", "ready"),
                               make_slot(3, "PETG", "Kingroon", "C47053", "ready")});
    nlohmann::json ace2_slots =
        nlohmann::json::array({make_slot(0, "PLA", "Polymaker", "1E9E4A", "ready"),
                               make_slot(1, "PLA", "eSUN", "19C3D6", "empty"),
                               make_slot(2, "PLA", "Jayo", "E72F1D", "ready"),
                               make_slot(3, "TPU", "Overture", "FFFFFF", "ready")});

    // Which tool is on the carriage, whether each channel holds filament, and
    // the per-tool nozzle. The AMS backend reads mount from the extruder pins
    // (NOT from toolhead.extruder, which Klipper always populates), presence
    // from print_task_config.filament_exist, and the step bar's live nozzle
    // readout from the extruder temperatures.
    nlohmann::json extruders = nlohmann::json::object();
    for (int h = 0; h < NUM_HEADS; ++h) {
        extruders[h == 0 ? "extruder" : fmt::format("extruder{}", h)] = {
            {"extruder_index", h},     {"active_pin", false},      {"park_pin", true},
            {"grab_valid_pin", false}, {"activating_move", false}, {"temperature", 30.0},
            {"target", 0.0},           {"can_extrude", false}};
    }

    status_ = {
        {"filament_feed left", std::move(left)},
        {"filament_feed right", std::move(right)},
        {"toolhead", {{"extruder", "extruder"}}},
        {"print_task_config", {{"filament_exist", {true, true, true, false}}}},
        {"ace",
         {{"mode", "head"},
          {"device_count", 2},
          {"active_device", 0},
          {"api_version", 2},
          {"status", "ready"},
          {"swap_in_progress", false},
          {"swap_phase", "idle"},
          {"last_swap_result", nullptr},
          {"head_ace", std::move(head_ace)},
          {"head_feeder", std::move(head_feeder)},
          {"head_manual", std::move(head_manual)},
          {"head_source", std::move(head_source)},
          {"aces", nlohmann::json::array({make_ace_unit(0, 31, 35, ace1_slots, {1, 1, 1, 1}),
                                          make_ace_unit(1, 26, 58, ace2_slots, {1, 0, 1, 1})})}}}};

    // Seat what the seeded machine has loaded, then publish once. The backend
    // builds its units from this first frame.
    for (int h = 0; h < NUM_HEADS; ++h) {
        if (SEED_HEAD_ACE[h] >= 0) {
            set_seat(h, SEED_HEAD_ACE[h], SEED_HEAD_BAY[h]);
        }
    }
    set_mounted(0);
    set_channel(0, "load_finish");
    set_detected(0, true, true);
    // A machine mid-print, not a cold one. That is the state a multiACE swap
    // actually happens in, and it is what lets a load dispatch without first
    // sitting through the UI's own preheat: AmsOperationSidebar skips its
    // preheat when the nozzle is already within 5 C of the effective target
    // (the hotter of the material temp and the last non-zero target).
    set_nozzle(0, LOAD_TEMP, LOAD_TEMP);
    publish();

    heater_timer_ = lv_timer_create(
        [](lv_timer_t* t) { static_cast<ScriptedU1*>(lv_timer_get_user_data(t))->tick_heaters(); },
        200, this);
    spdlog::info("[ScriptedU1] Seeded: U1 + 2x ACE, head 0 loaded hot from ACE 1 bay 1");
}

// ============================================================================
// Frame authoring
// ============================================================================

const char* ScriptedU1::feed_key(int head) {
    return head < 2 ? "filament_feed left" : "filament_feed right";
}

std::string ScriptedU1::channel_key(int head) {
    return fmt::format("extruder{}", head);
}

nlohmann::json& ScriptedU1::channel(int head) {
    return status_[feed_key(head)][channel_key(head)];
}

void ScriptedU1::set_channel(int head, const char* channel_state) {
    channel(head)["channel_state"] = channel_state;
}

void ScriptedU1::set_detected(int head, bool detected, bool in_toolhead) {
    auto& ch = channel(head);
    ch["filament_detected"] = detected;
    ch["filament_in_toolhead"] = in_toolhead;
    ch["filament_at_extruder"] = in_toolhead;
}

void ScriptedU1::set_seat(int head, int ace_index, int bay) {
    const auto& slots = status_["ace"]["aces"][ace_index]["slots"];
    const auto& slot = slots[bay];
    const auto& c = slot["color"];
    status_["ace"]["head_source"][std::to_string(head)] = {
        {"ace_index", ace_index},
        {"slot", bay},
        {"type", slot.value("material", "")},
        {"brand", slot.value("brand", "")},
        {"color",
         fmt::format("{:02X}{:02X}{:02X}", c[0].get<int>(), c[1].get<int>(), c[2].get<int>())}};
}

void ScriptedU1::clear_seat(int head) {
    status_["ace"]["head_source"][std::to_string(head)] = nullptr;
}

void ScriptedU1::set_mounted(int head) {
    // Exactly one head is on the carriage. "every one parked" is a real answer
    // (an empty carriage), which is why the backend reads the pins rather than
    // trusting toolhead.extruder — so the whole set has to be rewritten, not
    // just the winner.
    for (int h = 0; h < NUM_HEADS; ++h) {
        const std::string key = h == 0 ? "extruder" : fmt::format("extruder{}", h);
        status_[key]["active_pin"] = (h == head);
        status_[key]["park_pin"] = (h != head);
    }
    status_["toolhead"]["extruder"] = head == 0 ? "extruder" : fmt::format("extruder{}", head);
    status_["ace"]["active_device"] = head;
}

void ScriptedU1::set_filament_exist(int head, bool present) {
    status_["print_task_config"]["filament_exist"][head] = present;
}

void ScriptedU1::set_nozzle(int head, double temperature, double target) {
    Heater& h = heaters_[head];
    h.temp = temperature;
    h.target = target;
    h.from = temperature;
    h.dur = 0; // parked, not ramping
    const std::string key = head == 0 ? "extruder" : fmt::format("extruder{}", head);
    status_[key]["temperature"] = temperature;
    status_[key]["target"] = target;
    status_[key]["can_extrude"] = temperature > 170.0;
}

uint32_t ScriptedU1::begin_heat(int head, double target, uint32_t ramp_ms) {
    Heater& h = heaters_[head];
    h.from = h.temp;
    h.target = target;
    h.t0 = lv_tick_get();
    const double span = std::abs(target - h.from);
    h.dur = span < 2.0 ? 0
                       : static_cast<uint32_t>(static_cast<double>(ramp_ms) *
                                               std::min(1.0, span / FULL_HEAT_SPAN));
    spdlog::debug("[ScriptedU1] heater {}: {:.0f} -> {:.0f} C over {} ms", head, h.from, h.target,
                  h.dur);
    return h.dur;
}

void ScriptedU1::hold_next_for(uint32_t ms) {
    if (!steps_.empty()) {
        steps_.front().delay_ms = std::max(ms, MIN_HEAT_DWELL_MS);
    }
}

void ScriptedU1::tick_heaters() {
    // 5 Hz so a 3-second ramp is ~15 samples: the step bar's live readout counts
    // up visibly instead of jumping between three values.
    nlohmann::json delta = nlohmann::json::object();
    for (int i = 0; i < NUM_HEADS; ++i) {
        Heater& h = heaters_[i];
        double next;
        if (h.dur > 0) {
            const uint32_t elapsed = lv_tick_elaps(h.t0);
            if (elapsed >= h.dur) {
                h.dur = 0;
                next = h.target;
                spdlog::debug("[ScriptedU1] heater {}: settled at {:.0f} C", i, h.target);
            } else {
                // Linear, and deliberately deadline-based rather than a fixed
                // degrees-per-second: the ramp then lands ON target at exactly
                // ramp_ms whatever distance it had to cover.
                const double f = static_cast<double>(elapsed) / static_cast<double>(h.dur);
                next = h.from + (h.target - h.from) * f;
            }
        } else if (h.target > 0.0) {
            // Held at an active target. A real heater under PID sits on its
            // setpoint; the degree of wander that reads as "live" on an idle
            // nozzle reads as random noise on one the step bar is asking the
            // user to watch. Hold it exactly.
            next = h.target;
        } else {
            // Cold and unheated: drift around ambient so the readout is not a
            // frozen number.
            next = 30.0 + ((static_cast<int>(lv_tick_get() / 1000) + i) % 3) - 1.0;
        }
        if (std::abs(next - h.temp) < 0.05 && h.dur == 0) {
            continue; // nothing worth publishing
        }
        h.temp = next;
        const std::string key = i == 0 ? "extruder" : fmt::format("extruder{}", i);
        status_[key]["temperature"] = h.temp;
        status_[key]["target"] = h.target;
        status_[key]["can_extrude"] = h.temp > 170.0;
        delta[key] = status_[key];
    }
    if (!delta.empty()) {
        publish_delta(delta);
    }
}

void ScriptedU1::set_swap(bool in_progress, const char* phase) {
    status_["ace"]["swap_in_progress"] = in_progress;
    status_["ace"]["swap_phase"] = phase;
}

void ScriptedU1::publish_delta(const nlohmann::json& delta) {
    handle_status_update(delta);
    get_printer_state().update_from_status(delta);
}

void ScriptedU1::publish() {
    // One frame, both consumers — which is what the notify stream does. The AMS
    // backend reads filament_feed/ace; PrinterState reads the extruder objects
    // and owns the temperature subjects the sidebar's preheat gate and the Heat
    // step's live readout are bound to. Publishing to only one of them left the
    // nozzle reading 0 C to every part of the UI that asks PrinterState.
    handle_status_update(status_);
    get_printer_state().update_from_status(status_);
}

// ============================================================================
// Timeline
// ============================================================================

void ScriptedU1::enqueue(Step step) {
    steps_.push_back(std::move(step));
    if (!timer_) {
        timer_ = lv_timer_create(pump_cb, steps_.front().delay_ms, this);
    }
}

void ScriptedU1::pump_cb(lv_timer_t* timer) {
    static_cast<ScriptedU1*>(lv_timer_get_user_data(timer))->pump();
}

void ScriptedU1::pump() {
    if (steps_.empty()) {
        abort_script();
        return;
    }
    Step step = std::move(steps_.front());
    steps_.pop_front();
    if (step.apply) {
        step.apply();
    }
    publish();
    spdlog::debug("[ScriptedU1] frame '{}' ({} left)", step.note, steps_.size());

    if (steps_.empty()) {
        abort_script();
        return;
    }
    lv_timer_set_period(timer_, steps_.front().delay_ms);
}

void ScriptedU1::abort_script() {
    steps_.clear();
    if (timer_) {
        lv_timer_delete(timer_);
        timer_ = nullptr;
    }
}

// ============================================================================
// The scripts
//
// Timings are a plausible U1, not a measurement: heat dominates, the bowden
// feed is the long move, homing and picking are quick. Replace them wholesale
// the day a real capture with timestamps exists — that is the only part of this
// file a capture would change.
// ============================================================================

void ScriptedU1::script_load(int head, int settle_ms) {
    enqueue({static_cast<uint32_t>(settle_ms), [this, head] { set_channel(head, "load_prepare"); },
             "load_prepare"});
    enqueue({500, [this, head] { set_channel(head, "load_homing"); }, "load_homing"});
    enqueue({700, [this, head] { set_channel(head, "load_picking"); }, "load_picking"});
    enqueue({700,
             [this, head] {
                 set_mounted(head);
                 set_channel(head, "load_heating");
                 hold_next_for(begin_heat(head, LOAD_TEMP, HEAT_MS));
             },
             "load_heating"});
    // One frame, HEAT_MS long: the heater owns the readout in between, so the
    // Heat step ends exactly when the ramp lands.
    enqueue({HEAT_MS, [this, head] { set_channel(head, "load_feeding"); }, "load_feeding"});
    enqueue({1200,
             [this, head] {
                 set_channel(head, "load_extruding");
                 set_detected(head, true, true);
             },
             "load_extruding"});
    enqueue({900, [this, head] { set_channel(head, "load_flushing"); }, "load_flushing"});
    enqueue({900,
             [this, head] {
                 // Terminal: classify_channel_state sets the loaded latch here.
                 set_channel(head, "load_finish");
                 set_filament_exist(head, true);
                 set_swap(false, "idle");
             },
             "load_finish"});
}

void ScriptedU1::script_unload(int head, bool ends_at_preload_finish) {
    enqueue({250, [this, head] { set_channel(head, "unload_prepare"); }, "unload_prepare"});
    enqueue({500, [this, head] { set_channel(head, "unload_homing"); }, "unload_homing"});
    enqueue({600, [this, head] { set_channel(head, "unload_picking"); }, "unload_picking"});
    enqueue({600,
             [this, head] {
                 set_mounted(head);
                 set_channel(head, "unload_heating");
                 hold_next_for(begin_heat(head, LOAD_TEMP, HEAT_MS));
             },
             "unload_heating"});
    enqueue(
        {HEAT_MS, [this, head] { set_channel(head, "unload_heat_finish"); }, "unload_heat_finish"});
    enqueue({1200,
             [this, head] {
                 set_channel(head, "unload_doing");
                 set_detected(head, true, false);
             },
             "unload_doing"});
    if (ends_at_preload_finish) {
        // An ACE-fed head hands the retract to the ACE, so the U1's channel_state
        // stops here and `unload_finish` never arrives. Mid-swap the backend
        // treats this as a boundary rather than an end
        // (AmsBackendMultiAce::preload_finish_ends_unload).
        enqueue({700,
                 [this, head] {
                     set_channel(head, "preload_finish");
                     set_filament_exist(head, false);
                     clear_seat(head);
                 },
                 "preload_finish"});
    } else {
        enqueue({700,
                 [this, head] {
                     set_channel(head, "unload_finish");
                     set_detected(head, false, false);
                     set_filament_exist(head, false);
                     begin_heat(head, 0.0, 4000); // cool off after the retract
                     clear_seat(head);
                 },
                 "unload_finish"});
    }
}

// ============================================================================
// The wire
// ============================================================================

AmsError ScriptedU1::execute_gcode(const std::string& gcode) {
    spdlog::info("[ScriptedU1] <- {}", gcode);

    auto arg = [&gcode](const char* key) -> int {
        const size_t at = gcode.find(key);
        if (at == std::string::npos) {
            return -1;
        }
        return std::atoi(gcode.c_str() + at + std::strlen(key));
    };

    // --- multiACE: an ACE-fed head is driven by ACE_*, never the native path ---
    if (gcode.rfind("ACE_UNLOAD_HEAD", 0) == 0) {
        const int head = arg("HEAD=");
        set_swap(true, "unloading");
        // Ends at preload_finish: the ACE performs the retract.
        script_unload(head, /*ends_at_preload_finish=*/true);
        return AmsErrorHelper::success();
    }
    if (gcode.rfind("ACE_LOAD_HEAD", 0) == 0) {
        const int head = arg("HEAD=");
        const int ace = arg("ACE=");
        const int bay = arg("SLOT=");
        if (ace >= 0 && bay >= 0) {
            // Seat the new bay up front: head_source is wiring, and the wiring
            // changes when the command is accepted, not when filament arrives.
            enqueue({100, [this, head, ace, bay] { set_seat(head, ace, bay); }, "seat"});
        }
        set_swap(true, "loading");
        script_load(head, /*settle_ms=*/200);
        return AmsErrorHelper::success();
    }

    // --- native U1 path (a head on its own feeder) ---
    if (gcode.rfind("AUTO_FEEDING", 0) == 0) {
        const int head = arg("EXTRUDER=");
        if (gcode.find("UNLOAD=1") != std::string::npos) {
            script_unload(head, /*ends_at_preload_finish=*/false);
        } else {
            script_load(head, /*settle_ms=*/200);
        }
        return AmsErrorHelper::success();
    }
    if (gcode.rfind("INNER_FILAMENT_UNLOAD", 0) == 0) {
        script_unload(get_current_tool(), /*ends_at_preload_finish=*/false);
        return AmsErrorHelper::success();
    }

    // T<n>: mounts the head. On this machine that is a carriage move, not a
    // feed, so it resolves immediately rather than running a script.
    if (gcode.size() >= 2 && gcode[0] == 'T' &&
        std::isdigit(static_cast<unsigned char>(gcode[1]))) {
        const int tool = std::atoi(gcode.c_str() + 1);
        enqueue({150,
                 [this, tool] {
                     status_["ace"]["active_device"] = tool;
                     status_["toolhead"] = {
                         {"extruder", tool == 0 ? "extruder" : fmt::format("extruder{}", tool)}};
                 },
                 "toolchange"});
        return AmsErrorHelper::success();
    }

    // Everything else (PARK_EXTRUDER, ACE_DRY, ACE_SET_AUTO_DRY, ...) is accepted
    // and has no visible consequence on these screens.
    return AmsErrorHelper::success();
}

AmsError ScriptedU1::execute_gcode(const std::string& gcode, std::function<void()> on_complete) {
    AmsError err = execute_gcode(gcode);
    if (on_complete) {
        on_complete();
    }
    return err;
}

} // namespace helix::wasm
