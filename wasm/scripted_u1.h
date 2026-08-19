// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ams_backend_multiace.h"

#include <deque>
#include <functional>
#include <string>

#include "hv/json.hpp"

namespace helix::wasm {

/**
 * @file scripted_u1.h
 * @brief A Snapmaker U1 + multiACE that answers commands with status frames.
 *
 * Derives from the REAL AmsBackendMultiAce rather than mocking the UI: every
 * dispatch rule, every latch, the step-bar phase mapping and the ACE-fed
 * head-vs-feeder split are production code here. The only thing replaced is the
 * two ends of the wire —
 *
 *   - execute_gcode() is intercepted instead of reaching Moonraker, and
 *   - the reply arrives as handle_status_update() frames on an lv_timer,
 *     in the shape the firmware actually publishes.
 *
 * So a load in the browser walks the same channel_state sequence a real U1
 * walks (`load_prepare` -> `load_homing` -> ... -> `load_finish`), which is what
 * makes the sidebar's step bar, the path canvas and the sensor chips move: they
 * are reacting to firmware state, not to a script that pokes them.
 *
 * The frame vocabulary is taken from the live capture in
 * tests/fixtures/snapmaker_u1/u1-multiace-head-mode-idle.json and from
 * classify_channel_state() in ams_backend_snapmaker.cpp, which is the table the
 * firmware states are read off.
 *
 * @see tests/unit/test_ams_backend_multiace.cpp — same construction
 *      (nullptr, nullptr) + handle_status_update(), driven by the same fixture.
 */
class ScriptedU1 : public AmsBackendMultiAce {
  public:
    ScriptedU1();
    ~ScriptedU1() override;

    /// Push the idle seed frame and bring the backend up. Call once, after
    /// construction, on the UI thread.
    void begin();

    /// True while a scripted operation still has frames to play.
    [[nodiscard]] bool busy() const {
        return !steps_.empty();
    }

    /// Abandon the remaining frames of the current operation (the Reset button
    /// and the harness's own reset both land here).
    void abort_script();

  protected:
    // The two ends of the wire.
    AmsError execute_gcode(const std::string& gcode) override;
    AmsError execute_gcode(const std::string& gcode, std::function<void()> on_complete) override;

  private:
    struct Step {
        uint32_t delay_ms;           ///< wait BEFORE applying
        std::function<void()> apply; ///< mutates status_
        const char* note = "";       ///< for the log
    };

    // --- timeline plumbing ---
    void enqueue(Step step);
    void pump(); ///< timer tick: apply one step, arm the next
    static void pump_cb(lv_timer_t* timer);
    void publish(); ///< hand the whole retained status_ to both consumers
    /// Hand ONE changed object over — what the heater tick emits, so a 5 Hz
    /// temperature update does not re-parse the filament and ACE trees.
    void publish_delta(const nlohmann::json& delta);

    // --- frame authoring ---
    void set_channel(int head, const char* channel_state);
    void set_detected(int head, bool detected, bool in_toolhead);
    void set_seat(int head, int ace_index, int bay);
    void set_mounted(int head);
    void set_filament_exist(int head, bool present);
    /// Park a nozzle AT a temperature with no ramp (seeding, and cooldown).
    void set_nozzle(int head, double temperature, double target);
    /// Ask for @p target and interpolate to it, taking @p ramp_ms for a full
    /// cold-to-hot span and proportionally less for a shorter one. The scripts
    /// call only this; they never write a temperature directly, because two
    /// writers on one reading is what made the Heat step look like noise.
    /// @return how long the ramp will actually take, for the caller to dwell on.
    uint32_t begin_heat(int head, double target, uint32_t ramp_ms);
    /// Re-time the next queued frame — how a heat step matches its dwell to the
    /// ramp it just started.
    void hold_next_for(uint32_t ms);
    void tick_heaters(); ///< 5 Hz: advance every ramp, publish the deltas
    void clear_seat(int head);
    void set_swap(bool in_progress, const char* phase);

    /// Feed frames for one direction. `terminal` is the state that ends it:
    /// "load_finish", "unload_finish", or "preload_finish" for the unload half
    /// of a swap, which is a boundary rather than an end on an ACE-fed head.
    void script_load(int head, int settle_ms);
    void script_unload(int head, bool ends_at_preload_finish);

    [[nodiscard]] static const char* feed_key(int head);
    [[nodiscard]] static std::string channel_key(int head);
    [[nodiscard]] nlohmann::json& channel(int head);

    /// One heater per head. `from`/`t0`/`dur` describe an in-flight ramp; when
    /// `dur` is 0 the heater is settled and only wanders.
    struct Heater {
        double temp = 30.0;
        double target = 0.0;
        double from = 30.0;
        uint32_t t0 = 0;
        uint32_t dur = 0;
    };
    Heater heaters_[4];

    /// Retained full status. Frames are deltas on the wire, but keeping the whole
    /// document and publishing it wholesale is simpler and equally valid — the
    /// parser is written to accept either.
    nlohmann::json status_;
    std::deque<Step> steps_;
    lv_timer_t* timer_ = nullptr;        ///< advances the frame timeline
    lv_timer_t* heater_timer_ = nullptr; ///< advances every heater ramp
};

} // namespace helix::wasm
