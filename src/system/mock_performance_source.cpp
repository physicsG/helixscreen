// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#include "mock_performance_source.h"

#include "memory_utils.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>

namespace helix {
namespace perf {

namespace {

bool read_proc_stat(uint64_t& idle, uint64_t& total) {
    std::ifstream f("/proc/stat");
    if (!f)
        return false;
    std::string label;
    uint64_t user = 0, nice = 0, sys = 0, idl = 0, iowait = 0, irq = 0, softirq = 0, steal = 0;
    f >> label >> user >> nice >> sys >> idl >> iowait >> irq >> softirq >> steal;
    if (label != "cpu")
        return false;
    idle = idl + iowait;
    total = user + nice + sys + idl + iowait + irq + softirq + steal;
    return true;
}

bool read_meminfo(uint32_t& free_mb, float& pct_used) {
    const MemoryInfo mem = get_system_memory_info();
    const uint64_t total_kb = mem.total_kb, avail_kb = mem.available_kb;
    if (!total_kb || !avail_kb)
        return false;
    free_mb = static_cast<uint32_t>(avail_kb / 1024);
    pct_used = 100.0f * (1.0f - static_cast<float>(avail_kb) / total_kb);
    return true;
}

bool read_thermal(float& temp_c) {
    std::ifstream f("/sys/class/thermal/thermal_zone0/temp");
    if (!f)
        return false;
    int milli;
    if (!(f >> milli))
        return false;
    temp_c = milli / 1000.0f;
    return true;
}

} // namespace

MockPerformanceSource::MockPerformanceSource() = default;
MockPerformanceSource::~MockPerformanceSource() {
    stop();
}

void MockPerformanceSource::start() {
    if (running_.exchange(true))
        return;
    timer_ = lv_timer_create(
        [](lv_timer_t* t) {
            auto* self = static_cast<MockPerformanceSource*>(lv_timer_get_user_data(t));
            self->tick();
        },
        1000, this);
}

void MockPerformanceSource::stop() {
    if (!running_.exchange(false))
        return;
    if (timer_) {
        lv_timer_delete(timer_);
        timer_ = nullptr;
    }
    lifetime_.invalidate();
}

bool MockPerformanceSource::read_host_stats(PerfSample& out) {
    uint64_t idle, total;
    if (read_proc_stat(idle, total)) {
        if (have_prev_) {
            const uint64_t d_total = total - prev_total_;
            const uint64_t d_idle = idle - prev_idle_;
            if (d_total > 0) {
                out.host_cpu_pct = 100.0f * (1.0f - static_cast<float>(d_idle) / d_total);
            }
        }
        prev_idle_ = idle;
        prev_total_ = total;
        have_prev_ = true;
    }
    uint32_t mem_free = 0;
    float mem_used = 0.0f;
    if (read_meminfo(mem_free, mem_used)) {
        out.host_mem_free_mb = mem_free;
        out.host_mem_pct_used = mem_used;
    }
    float t = 0.0f;
    if (read_thermal(t)) {
        out.host_cpu_temp_c = t;
    }
    return true;
}

void MockPerformanceSource::synth_mcus(PerfSample& out) {
    std::uniform_real_distribution<float> step(-0.04f, 0.04f);
    mcu_load_ = std::clamp(mcu_load_ + step(rng_), 0.05f, 0.45f);
    mcu_sb_load_ = std::clamp(mcu_sb_load_ + step(rng_), 0.05f, 0.45f);

    McuStat a;
    a.name = "mcu";
    a.load = mcu_load_;
    a.retransmits = 0;
    McuStat b;
    b.name = "mcu sb";
    b.load = mcu_sb_load_;
    b.retransmits = static_cast<uint64_t>((tick_count_ / 30) * 14);
    out.mcus = {a, b};
}

void MockPerformanceSource::apply_throttle_env(PerfSample& out) {
    const char* env = std::getenv("HELIX_MOCK_THROTTLE");
    if (!env || !*env)
        return;
    const std::string e = env;
    out.host_throttle_bits = 0x50000;
    out.host_throttle_text = "Under-voltage detected (now)";
    if (e == "freq_capped_prev") {
        out.host_throttle_bits = 0x40000;
        out.host_throttle_text = "Frequency previously capped";
    }
}

void MockPerformanceSource::tick() {
    if (!running_ || !cb_)
        return;
    PerfSample s;
    read_host_stats(s);
    synth_mcus(s);
    apply_throttle_env(s);
    ++tick_count_;
    cb_(s);
}

} // namespace perf
} // namespace helix
