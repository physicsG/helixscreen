// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace helix::calibration {

/// Why the belt tension check cannot run, or OK if it can.
enum class BeltGate {
    OK,
    NOT_CONNECTED,    ///< no printer connection or klippy is not ready
    NO_ACCELEROMETER, ///< no adxl345/lis2dw/... section in printer.cfg
    DETECTING,        ///< kinematics not known yet; hardware detection is running
    NOT_COREXY,       ///< the A/B belt-path model does not apply
    NOT_COLOCATED,    ///< klippy's UDS socket is not reachable from here
    PRINTING,         ///< a print job owns the toolhead
};

struct BeltGateInputs {
    bool connected = false;
    bool has_accelerometer = false;
    bool is_corexy = false;
    bool detecting = false; ///< hardware detection has not answered yet
    bool klippy_socket_reachable = false;
    bool print_active = false;
};

/**
 * @brief Decide whether the belt tension check can run
 *
 * Permanent blockers are reported before transient ones. A bed slinger that is
 * also mid-print reports NOT_COREXY, not PRINTING - telling that user to wait
 * for the print to finish promises something that will never arrive.
 */
BeltGate evaluate_belt_gate(const BeltGateInputs& in);

/// Untranslated explanatory text for a gate. Never null, never empty.
const char* belt_gate_message(BeltGate gate);

} // namespace helix::calibration
