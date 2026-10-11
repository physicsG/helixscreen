// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#include "belt_gating.h"
#include "printer_state.h"

#include "../catch_amalgamated.hpp"

using helix::calibration::belt_gate_message;
using helix::calibration::BeltGate;
using helix::calibration::BeltGateInputs;
using helix::calibration::evaluate_belt_gate;

namespace {

/// Every gate satisfied - the shape the panel sees on a healthy reference Voron.
BeltGateInputs all_clear() {
    BeltGateInputs in;
    in.connected = true;
    in.has_accelerometer = true;
    in.is_corexy = true;
    in.klippy_socket_reachable = true;
    in.print_active = false;
    return in;
}

} // namespace

TEST_CASE("a healthy co-located CoreXY passes every gate", "[belt][gating]") {
    CHECK(evaluate_belt_gate(all_clear()) == BeltGate::OK);
}

TEST_CASE("each blocker is reported", "[belt][gating]") {
    auto in = all_clear();
    in.connected = false;
    CHECK(evaluate_belt_gate(in) == BeltGate::NOT_CONNECTED);

    in = all_clear();
    in.has_accelerometer = false;
    CHECK(evaluate_belt_gate(in) == BeltGate::NO_ACCELEROMETER);

    in = all_clear();
    in.is_corexy = false;
    CHECK(evaluate_belt_gate(in) == BeltGate::NOT_COREXY);

    in = all_clear();
    in.klippy_socket_reachable = false;
    CHECK(evaluate_belt_gate(in) == BeltGate::NOT_COLOCATED);

    in = all_clear();
    in.print_active = true;
    CHECK(evaluate_belt_gate(in) == BeltGate::PRINTING);
}

TEST_CASE("permanent blockers outrank an active print", "[belt][gating]") {
    // A bed slinger mid-print must not be told to wait for the print to end -
    // the feature will never work there, and "wait" is a promise we cannot keep.
    auto in = all_clear();
    in.is_corexy = false;
    in.print_active = true;
    CHECK(evaluate_belt_gate(in) == BeltGate::NOT_COREXY);

    in = all_clear();
    in.has_accelerometer = false;
    in.print_active = true;
    CHECK(evaluate_belt_gate(in) == BeltGate::NO_ACCELEROMETER);
}

TEST_CASE("unknown kinematics while detection runs is not a verdict", "[belt][gating]") {
    // Detection takes tens of seconds after a fresh start; until it answers the
    // printer is not known to be a bed slinger, so the panel must not say so.
    auto in = all_clear();
    in.is_corexy = false;
    in.detecting = true;
    CHECK(evaluate_belt_gate(in) == BeltGate::DETECTING);

    in.detecting = false;
    CHECK(evaluate_belt_gate(in) == BeltGate::NOT_COREXY);

    // A CoreXY answer wins even if the pending flag has not been cleared yet.
    in = all_clear();
    in.detecting = true;
    CHECK(evaluate_belt_gate(in) == BeltGate::OK);
}

TEST_CASE("disconnection outranks everything", "[belt][gating]") {
    BeltGateInputs in{}; // all false, including connected
    CHECK(evaluate_belt_gate(in) == BeltGate::NOT_CONNECTED);
}

TEST_CASE("every gate has a non-empty message", "[belt][gating]") {
    for (auto g :
         {BeltGate::OK, BeltGate::NOT_CONNECTED, BeltGate::NO_ACCELEROMETER, BeltGate::DETECTING,
          BeltGate::NOT_COREXY, BeltGate::NOT_COLOCATED, BeltGate::PRINTING}) {
        const char* m = belt_gate_message(g);
        REQUIRE(m != nullptr);
        CHECK(m[0] != '\0');
    }
}

TEST_CASE("gate inputs assembled at panel entry reflect a fresh connection",
          "[belt][gating][panel]") {
    // At panel entry the accelerometer subject may still be 0 - discovery
    // populates it from configfile.config, which arrives after connect. A gate
    // that only ever ran against a settled value would pass here and then let
    // a user press Start on a printer with no accelerometer.
    BeltGateInputs in;
    in.connected = true;
    in.has_accelerometer = false; // not yet discovered
    in.is_corexy = true;
    in.klippy_socket_reachable = true;
    in.print_active = false;
    CHECK(evaluate_belt_gate(in) == BeltGate::NO_ACCELEROMETER);
}

TEST_CASE("belt_path_kinematics matches only the kinematics whose diagonals are belt paths",
          "[belt][kinematics]") {
    using helix::belt_path_kinematics;
    // Klipper names are lowercase; no case folding.
    CHECK(belt_path_kinematics("corexy"));
    CHECK(belt_path_kinematics("limited_corexy"));

    CHECK_FALSE(belt_path_kinematics("corexz"));
    CHECK_FALSE(belt_path_kinematics("limited_corexz"));
    CHECK_FALSE(belt_path_kinematics("hybrid_corexy"));
    CHECK_FALSE(belt_path_kinematics("hybrid_corexz"));
    CHECK_FALSE(belt_path_kinematics("cartesian"));
    CHECK_FALSE(belt_path_kinematics("limited_cartesian"));
    CHECK_FALSE(belt_path_kinematics("delta"));
    CHECK_FALSE(belt_path_kinematics("rotary_delta"));
    CHECK_FALSE(belt_path_kinematics("deltesian"));
    CHECK_FALSE(belt_path_kinematics("polar"));
    CHECK_FALSE(belt_path_kinematics("winch"));
    CHECK_FALSE(belt_path_kinematics("none"));
    CHECK_FALSE(belt_path_kinematics(""));
    CHECK_FALSE(belt_path_kinematics("CoreXY"));
}
