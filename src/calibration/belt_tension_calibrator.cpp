// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file belt_tension_calibrator.cpp
 * @brief Implementation of BeltTensionCalibrator
 *
 * One measure_path() call homes if needed, then sweeps one belt path through
 * IAdvancedAPI::test_belt_resonance(), marshalling every off-thread API
 * callback onto the LVGL thread via lifetime_.bg_cb().
 */

#include "belt_tension_calibrator.h"

#include "calibration_abort.h"
#include "i_moonraker_api.h"
#include "spdlog/spdlog.h"
#include "toolhead_homing.h"

namespace helix::calibration {

// ============================================================================
// Constructors / Destructor
// ============================================================================

BeltTensionCalibrator::BeltTensionCalibrator() : api_(nullptr) {
    spdlog::debug("[BeltTension] Created without API (test mode)");
}

BeltTensionCalibrator::BeltTensionCalibrator(IMoonrakerAPI* api) : api_(api) {
    spdlog::debug("[BeltTension] Created with API");
}

BeltTensionCalibrator::~BeltTensionCalibrator() {
    cancel();
}

// ============================================================================
// Path names
// ============================================================================

const char* BeltTensionCalibrator::axis_param(BeltPath p) {
    return p == BeltPath::PATH_A ? "1,-1" : "1,1";
}

const char* BeltTensionCalibrator::output_name(BeltPath p) {
    return p == BeltPath::PATH_A ? "helix_belt_a" : "helix_belt_b";
}

// ============================================================================
// detect_hardware() — thin wrapper around API
// ============================================================================

void BeltTensionCalibrator::detect_hardware(BeltHardwareDetectCallback on_complete,
                                            BeltErrorCallback on_error) {
    if (!api_) {
        spdlog::warn("[BeltTension] detect_hardware called without API");
        if (on_error) {
            on_error("No API available");
        }
        return;
    }

    state_.store(State::DETECTING_HARDWARE);
    spdlog::info("[BeltTension] Detecting printer hardware capabilities");

    api_->advanced().detect_belt_hardware(
        lifetime_.bg_cb("BeltTensionCalibrator::detect_hw_success",
                        [this, on_complete](const BeltTensionHardware& hw) {
                            hardware_ = hw;
                            state_.store(State::IDLE);
                            if (on_complete)
                                on_complete(hw);
                        }),
        lifetime_.bg_cb("BeltTensionCalibrator::detect_hw_error",
                        [this, on_error](const MoonrakerError& err) {
                            state_.store(State::ERROR);
                            if (on_error)
                                on_error("Hardware detection failed: " + err.message);
                        }));
}

// ============================================================================
// measure_path() — home, then sweep one belt diagonal
// ============================================================================

void BeltTensionCalibrator::measure_path(
    BeltPath path, std::function<void(int percent, float freq_hz)> on_progress,
    std::function<void(BeltCurve)> on_complete, BeltErrorCallback on_error) {
    if (!api_) {
        spdlog::warn("[BeltTension] measure_path called without API");
        if (on_error) {
            on_error("No printer connection");
        }
        return;
    }

    // A run may still be sweeping; stop listening to it before starting anew.
    cancel();
    const uint32_t generation = ++run_generation_;

    state_.store(State::HOMING);
    spdlog::info("[BeltTension] Measuring belt path {}", output_name(path));

    ensure_homed_then(
        api_, lifetime_,
        [this, generation, path, on_progress, on_complete, on_error]() {
            if (generation != run_generation_) {
                spdlog::debug("[BeltTension] Cancelled while homing; not starting the sweep");
                return;
            }
            state_.store(State::MEASURING);
            run_cancel_ = api_->advanced().test_belt_resonance(
                axis_param(path), output_name(path),
                lifetime_.bg_cb("BeltTensionCalibrator::sweep_progress",
                                [this, generation, on_progress](int percent, float freq_hz) {
                                    if (generation == run_generation_ && on_progress)
                                        on_progress(percent, freq_hz);
                                }),
                lifetime_.bg_cb("BeltTensionCalibrator::sweep_complete",
                                [this, generation, on_complete](const BeltCurve& curve) {
                                    if (generation != run_generation_)
                                        return;
                                    run_cancel_ = nullptr;
                                    state_.store(State::IDLE);
                                    if (on_complete)
                                        on_complete(curve);
                                }),
                lifetime_.bg_cb("BeltTensionCalibrator::sweep_error",
                                [this, generation, on_error](const MoonrakerError& err) {
                                    if (generation != run_generation_)
                                        return;
                                    run_cancel_ = nullptr;
                                    state_.store(State::ERROR);
                                    spdlog::error("[BeltTension] Sweep failed: {}", err.message);
                                    if (on_error)
                                        on_error(err.message);
                                }));
        },
        [this, generation, on_error](const MoonrakerError& err) {
            if (generation != run_generation_)
                return;
            state_.store(State::ERROR);
            spdlog::error("[BeltTension] Homing failed: {}", err.message);
            if (on_error)
                on_error(err.message);
        });
}

// ============================================================================
// cancel() / emergency_abort() / reset()
// ============================================================================

void BeltTensionCalibrator::cancel() {
    spdlog::info("[BeltTension] Cancelling (was state={})", static_cast<int>(state_.load()));
    ++run_generation_;
    if (run_cancel_) {
        run_cancel_();
        run_cancel_ = nullptr;
    }
    state_.store(State::IDLE);
}

void BeltTensionCalibrator::emergency_abort() {
    cancel();
    helix::emergency_stop_and_restart(api_, "BeltTensionCalibrator");
}

void BeltTensionCalibrator::reset() {
    spdlog::info("[BeltTension] Resetting calibrator");
    cancel();
    hardware_ = BeltTensionHardware{};
}

} // namespace helix::calibration
