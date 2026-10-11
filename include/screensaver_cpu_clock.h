// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <functional>

namespace helix::ui {

/// CPU time the whole process has used and the steady clock, read together.
struct CpuSample {
    uint64_t cpu_ns = 0;
    uint64_t wall_ns = 0;
};

/// Source of CpuSamples; tests pass a scripted one.
using CpuClockFn = std::function<CpuSample()>;

/// Reads CLOCK_PROCESS_CPUTIME_ID, which counts every thread of the process, LVGL's draw
/// thread included, and the steady clock.
CpuSample read_process_cpu_clock();

} // namespace helix::ui
