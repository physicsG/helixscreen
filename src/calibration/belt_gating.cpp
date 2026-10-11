// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#include "belt_gating.h"

namespace helix::calibration {

BeltGate evaluate_belt_gate(const BeltGateInputs& in) {
    if (!in.connected)
        return BeltGate::NOT_CONNECTED;
    if (!in.has_accelerometer)
        return BeltGate::NO_ACCELEROMETER;
    if (!in.is_corexy)
        return in.detecting ? BeltGate::DETECTING : BeltGate::NOT_COREXY;
    if (!in.klippy_socket_reachable)
        return BeltGate::NOT_COLOCATED;
    if (in.print_active)
        return BeltGate::PRINTING;
    return BeltGate::OK;
}

const char* belt_gate_message(BeltGate gate) {
    switch (gate) {
    case BeltGate::OK:
        return "Ready";
    case BeltGate::NOT_CONNECTED:
        return "Not connected to the printer";
    case BeltGate::NO_ACCELEROMETER:
        return "No accelerometer found in your Klipper config";
    case BeltGate::DETECTING:
        return "Checking the printer...";
    case BeltGate::NOT_COREXY:
        return "Belt Tension needs a CoreXY printer.";
    case BeltGate::NOT_COLOCATED:
        return "This needs HelixScreen running on the printer itself";
    case BeltGate::PRINTING:
        return "Wait until the print finishes";
    }
    return "Unavailable";
}

} // namespace helix::calibration
