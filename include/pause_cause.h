// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <vector>

namespace helix {

/// Why a print paused, for resume decision-making.
enum class PauseCause {
    Recoverable, ///< Attempt RESUME (matches stock + non-Snapmaker backends)
    Terminal,    ///< Print cannot resume; caller must offer "restart from beginning"
    Unknown,     ///< Insufficient signal; callers treat this exactly as Recoverable
};

/// Raw inputs for classification, all sourced from PrinterState.
struct PauseSignals {
    std::string message;         ///< print_stats.message (firmware reason text)
    int exception_id = -1;       ///< print_stats.exception id (-1 = unknown)
    int exception_code = -1;     ///< print_stats.exception code (-1 = unknown)
    bool sdcard_active = true;   ///< virtual_sdcard.is_active
    bool runout_tripped = false; ///< a runout sensor is currently latched
};

/// A single terminal-cause pattern. A signal is Terminal iff it matches a
/// matcher. Matchers are supplied per-backend; an EMPTY list means nothing is
/// terminal (every cause Recoverable), identical to the non-Snapmaker no-op
/// resume path.
struct TerminalMatcher {
    std::string message_substr;           ///< case-insensitive substring; "" = ignore
    int exception_id = -1;                ///< exact match; -1 = ignore
    bool require_sdcard_inactive = false; ///< if true, only when sdcard_active == false
    int exception_code = -1;              ///< exact match; -1 = ignore
};

/// Classify why a print paused. Pure: no LVGL, threading, or singletons.
PauseCause classify_pause(const PauseSignals& signals,
                          const std::vector<TerminalMatcher>& terminal_matchers);

} // namespace helix
