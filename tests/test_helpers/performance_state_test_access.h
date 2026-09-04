// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "performance_state.h"

namespace helix::perf {

class PerformanceStateTestAccess {
  public:
    static void apply_sample(PerformanceState& ps, const PerfSample& s) {
        ps.apply_sample(s);
    }
};

} // namespace helix::perf
