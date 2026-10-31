// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace helix {
namespace perf {

/// One MCU's per-tick stats. mcu_load is 0..1 (UI multiplies by 100).
struct McuStat {
    std::string name;                    ///< e.g. "mcu", "mcu sb"
    std::optional<float> load;           ///< awake/wall over the last sample window
    std::optional<uint64_t> retransmits; ///< cumulative counter; UI displays raw value
};

/// One full sample emitted by an IPerformanceSource. Optionals carry the
/// "present" semantics — absent = no data this tick (UI hides the row).
struct PerfSample {
    std::optional<float> host_cpu_pct;    ///< 0..100
    std::optional<float> host_cpu_temp_c; ///< °C
    std::optional<uint32_t> host_mem_free_mb;
    std::optional<float> host_mem_pct_used;     ///< 0..100
    std::optional<uint32_t> host_disk_total_mb; ///< gcodes disk, from a slow poll
    std::optional<uint32_t> host_disk_free_mb;
    uint32_t host_throttle_bits = 0; ///< Pi bitmask; 0 = no flags ever set
    std::string host_throttle_text;  ///< empty unless bits != 0
    std::vector<McuStat> mcus;       ///< sorted by name
};

/// Strategy interface — Moonraker in prod, /proc + synthetic in test/mock.
class IPerformanceSource {
  public:
    using SampleCallback = std::function<void(const PerfSample&)>;

    virtual ~IPerformanceSource() = default;

    /// Begin emitting samples. Idempotent.
    virtual void start() = 0;

    /// Stop and tear down (idempotent). Must be safe to call from main thread.
    virtual void stop() = 0;

    /// Whether anything on screen shows the gcodes disk's free space. A source that must
    /// spend a request to learn it reads it only while this is true.
    virtual void set_disk_wanted(bool /*wanted*/) {}

    /// Set the on-main-thread callback that receives each sample.
    /// Sources are responsible for deferring bg-thread work to main via
    /// AsyncLifetimeGuard before invoking this callback.
    virtual void set_callback(SampleCallback cb) = 0;
};

} // namespace perf
} // namespace helix
