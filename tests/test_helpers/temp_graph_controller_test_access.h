// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "temp_graph_controller.h"

#include <cstdint>
#include <functional>
#include <utility>

namespace helix {

// Replaces the wall clock TempGraphController stamps live samples with, so a
// test can cross a sample-slot boundary deterministically. Declared a friend of
// TempGraphController (see temp_graph_controller.h).
class TempGraphControllerTestAccess {
  public:
    static void set_clock(TempGraphController& c, std::function<int64_t()> now_ms) {
        c.now_ms_fn_ = std::move(now_ms);
    }
};

} // namespace helix
