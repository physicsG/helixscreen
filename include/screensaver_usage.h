// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <chrono>
#include <cstddef>
#include <map>
#include <optional>
#include <string>

/**
 * @file screensaver_usage.h
 * @brief How long each screensaver showed and how its runs ended, over one reporting window
 *
 * ScreensaverManager writes it on the main thread as runs start and stop; TelemetryManager reads
 * and resets a window on the same thread. No LVGL and no telemetry: always compiled, including in
 * builds without screensavers, where nothing writes it and every window is empty.
 */

namespace helix {

/// How a screensaver run ended.
enum class SaverRunEnd {
    TOUCH,     ///< The user woke the display.
    OTHER,     ///< Display sleep, a wake the app asked for, a switch to another saver, shutdown.
    TOO_HEAVY, ///< The gate measured it over budget at its lowest level and ran a fallback.
};

/// One window's numbers, each map keyed by the saver's stable registry name.
struct SaverUsageWindow {
    std::map<std::string, int> shown_sec;
    std::map<std::string, int> starts;
    std::map<std::string, int> woke_by_touch;
    std::map<std::string, int> stopped_other;
    std::map<std::string, int> too_heavy;
    /// Ended runs per `<name>:<level>`, the gate level each run settled at.
    std::map<std::string, int> settled_levels;

    bool empty() const;
};

/// Name the black screen is counted under when every saver is too heavy for the board.
inline constexpr const char* kSaverUsageBlackScreen = "black_screen";

/**
 * @brief Accumulates SaverUsageWindow from run starts and ends
 *
 * Seconds are counted to the millisecond and reported whole; the sub-second remainder carries
 * into the next window, so many short runs are not each rounded down to nothing.
 */
class SaverUsage {
  public:
    using Clock = std::chrono::steady_clock;

    /// A run of `name` starts at `now`, ending any run still open as OTHER.
    void begin(const std::string& name, Clock::time_point now);

    /// The open run ends at `now`, at gate `level` (none for the black screen). No-op with no run
    /// open.
    void end(SaverRunEnd how, std::optional<size_t> level, Clock::time_point now);

    bool running() const {
        return open_.has_value();
    }

    /// The window so far, with an open run's time up to `now` included.
    SaverUsageWindow window(Clock::time_point now) const;

    /// Starts the next window at `now`. An open run stays open and is counted from `now`.
    void reset_window(Clock::time_point now);

  private:
    struct OpenRun {
        std::string name;
        Clock::time_point since;
    };

    /// Shown milliseconds per name, including what the last reset carried over.
    std::map<std::string, int64_t> shown_ms_;
    SaverUsageWindow counts_;
    std::optional<OpenRun> open_;
};

} // namespace helix
