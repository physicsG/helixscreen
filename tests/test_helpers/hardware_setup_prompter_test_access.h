// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "hardware_setup_prompter.h"

#include <string>

namespace helix {

/// Raises the type mismatch warning as a Warn decision does, without the hardware
/// detection that decides it (covered by the PrinterDetector tests).
class HardwareSetupPrompterTestAccess {
  public:
    static void warn_type_mismatch(HardwareSetupPrompter& p, const std::string& detected,
                                   int confidence, const std::string& saved) {
        p.warn_type_mismatch({detected, confidence, saved});
    }
};

} // namespace helix
