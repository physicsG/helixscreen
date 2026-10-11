// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#include "belt_dsp_probe.h"
#include "pitch_estimator.h"
#include "platform_info.h"

#include "../catch_amalgamated.hpp"

using helix::calibration::cached_dsp_probe;
using helix::calibration::dsp_ms_is_capable;
using helix::calibration::MAX_PSD_MS;
using helix::calibration::PROBE_BANDWIDTH_HZ;
using helix::calibration::probe_dsp_throughput;

TEST_CASE("dsp_ms_is_capable brackets the threshold", "[belt][dsp_probe]") {
    CHECK(dsp_ms_is_capable(1.0));
    CHECK(dsp_ms_is_capable(MAX_PSD_MS - 0.001));
    CHECK_FALSE(dsp_ms_is_capable(MAX_PSD_MS + 0.001));
    CHECK_FALSE(dsp_ms_is_capable(10000.0));
}

TEST_CASE("dsp_ms_is_capable rejects a nonsense measurement", "[belt][dsp_probe]") {
    // A zero or negative elapsed time means the clock or the timing code is
    // broken. Reading that as "infinitely fast hardware" would enable the
    // feature on exactly the machines whose timing cannot be trusted.
    CHECK_FALSE(dsp_ms_is_capable(0.0));
    CHECK_FALSE(dsp_ms_is_capable(-1.0));
}

TEST_CASE("probe measures a positive elapsed time", "[belt][dsp_probe]") {
    const auto r = probe_dsp_throughput();
    CHECK(r.psd_ms > 0.0);
    CHECK(r.capable == dsp_ms_is_capable(r.psd_ms));
}

TEST_CASE("the printer's own board clears the DSP budget", "[belt][dsp_probe]") {
    // MAX_PSD_MS is a budget for the board the pluck path actually runs on, so
    // this measurement only means something when the binary is running there.
    // Off the printer it times the host instead: a dev laptop or CI runner
    // answers a different question, and answers it wrong the moment anything
    // else on the box is compiling — a parallel shard run has measured 89.9 ms
    // against this 60 ms threshold with the code unchanged.
    if (!helix::is_printer_embedded()) {
        SKIP("Not running on the printer: this would measure the host, not the "
             "board MAX_PSD_MS is a budget for.");
    }

    const auto r = probe_dsp_throughput();
    INFO("measured psd_ms = " << r.psd_ms << ", threshold = " << MAX_PSD_MS);
    CHECK(r.capable);
}

TEST_CASE("cached probe returns a stable result", "[belt][dsp_probe]") {
    const auto& a = cached_dsp_probe();
    const auto& b = cached_dsp_probe();
    CHECK(&a == &b);
    CHECK(a.psd_ms == b.psd_ms);
}

TEST_CASE("the probe measures at least the bandwidth production requests", "[belt][dsp_probe]") {
    // If the search window or harmonic count ever widens, the probe must not
    // silently start timing a cheaper transform than the pluck path runs -
    // the gate would still pass while the real work no longer fits the budget.
    float lo = 0.0f, hi = 0.0f;
    REQUIRE(helix::calibration::search_window_for_span(helix::calibration::REFERENCE_SPAN_MM, &lo,
                                                       &hi));
    CHECK(PROBE_BANDWIDTH_HZ >=
          helix::calibration::required_bandwidth_hz(hi, helix::calibration::DEFAULT_HARMONICS));
}
