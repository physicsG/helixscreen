// tests/test_helpers/belt_tension_calibrator_test_access.h
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "belt_tension_calibrator.h"

// Friend-class shim for BeltTensionCalibrator (declared as
// `friend class BeltTensionCalibratorTestAccess;` in the calibrator header).
//
// The strobe entry points are gated on hardware_ having a [pwm_cycle_time]
// pin, which only detect_hardware() ever fills in and which no mock printer
// profile advertises. Seeding it here lets a test drive the real strobe calls
// instead of re-implementing them.
class BeltTensionCalibratorTestAccess {
  public:
    static void seed_pwm_led(helix::calibration::BeltTensionCalibrator& cal,
                             const std::string& pin_name) {
        cal.hardware_.has_pwm_led = true;
        cal.hardware_.pwm_led_pin = pin_name;
    }

    static void force_strobe_mode(helix::calibration::BeltTensionCalibrator& cal) {
        cal.state_.store(helix::calibration::BeltTensionCalibrator::State::STROBE_MODE);
    }
};
