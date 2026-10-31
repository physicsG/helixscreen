// SPDX-License-Identifier: GPL-3.0-or-later

#include "screensaver_usage.h"

#include <chrono>

#include "../catch_amalgamated.hpp"

using namespace std::chrono_literals;
using helix::SaverRunEnd;
using helix::SaverUsage;
using helix::SaverUsageWindow;

namespace {

const SaverUsage::Clock::time_point T0 = SaverUsage::Clock::time_point{} + 1000s;

} // namespace

TEST_CASE("SaverUsage counts a run's start, its seconds, how it ended and its level",
          "[screensaver][screensaver_usage]") {
    SaverUsage usage;
    usage.begin("maze", T0);
    CHECK(usage.running());
    usage.end(SaverRunEnd::TOUCH, 2, T0 + 90s);
    CHECK_FALSE(usage.running());

    const SaverUsageWindow w = usage.window(T0 + 100s);
    CHECK(w.starts == std::map<std::string, int>{{"maze", 1}});
    CHECK(w.shown_sec == std::map<std::string, int>{{"maze", 90}});
    CHECK(w.woke_by_touch == std::map<std::string, int>{{"maze", 1}});
    CHECK(w.stopped_other.empty());
    CHECK(w.too_heavy.empty());
    CHECK(w.settled_levels == std::map<std::string, int>{{"maze:2", 1}});
}

TEST_CASE("SaverUsage keeps each ending apart, per saver", "[screensaver][screensaver_usage]") {
    SaverUsage usage;
    usage.begin("maze", T0);
    usage.end(SaverRunEnd::TOO_HEAVY, 4, T0 + 10s);
    usage.begin("pipes", T0 + 10s);
    usage.end(SaverRunEnd::OTHER, 0, T0 + 40s);
    usage.begin("pipes", T0 + 50s);
    usage.end(SaverRunEnd::TOUCH, 1, T0 + 55s);

    const SaverUsageWindow w = usage.window(T0 + 60s);
    CHECK(w.starts == std::map<std::string, int>{{"maze", 1}, {"pipes", 2}});
    CHECK(w.shown_sec == std::map<std::string, int>{{"maze", 10}, {"pipes", 35}});
    CHECK(w.too_heavy == std::map<std::string, int>{{"maze", 1}});
    CHECK(w.stopped_other == std::map<std::string, int>{{"pipes", 1}});
    CHECK(w.woke_by_touch == std::map<std::string, int>{{"pipes", 1}});
    CHECK(w.settled_levels ==
          std::map<std::string, int>{{"maze:4", 1}, {"pipes:0", 1}, {"pipes:1", 1}});
}

TEST_CASE("SaverUsage: a run with no level records its ending but no settled level",
          "[screensaver][screensaver_usage]") {
    SaverUsage usage;
    usage.begin(helix::kSaverUsageBlackScreen, T0);
    usage.end(SaverRunEnd::TOUCH, std::nullopt, T0 + 5s);

    const SaverUsageWindow w = usage.window(T0 + 5s);
    CHECK(w.woke_by_touch == std::map<std::string, int>{{"black_screen", 1}});
    CHECK(w.settled_levels.empty());
}

TEST_CASE("SaverUsage: starting a run while one is open ends the open one as other",
          "[screensaver][screensaver_usage]") {
    SaverUsage usage;
    usage.begin("maze", T0);
    usage.begin("pipes", T0 + 20s);

    const SaverUsageWindow w = usage.window(T0 + 30s);
    CHECK(w.stopped_other == std::map<std::string, int>{{"maze", 1}});
    CHECK(w.shown_sec == std::map<std::string, int>{{"maze", 20}, {"pipes", 10}});
    CHECK(w.settled_levels.empty());
}

TEST_CASE("SaverUsage: ending with no run open changes nothing",
          "[screensaver][screensaver_usage]") {
    SaverUsage usage;
    usage.end(SaverRunEnd::TOUCH, 0, T0);
    CHECK(usage.window(T0 + 1s).empty());
}

TEST_CASE("SaverUsage: an open run's time is in the window and the next window counts from the "
          "reset",
          "[screensaver][screensaver_usage]") {
    SaverUsage usage;
    usage.begin("maze", T0);

    const SaverUsageWindow first = usage.window(T0 + 30s);
    CHECK(first.shown_sec == std::map<std::string, int>{{"maze", 30}});
    CHECK(first.starts == std::map<std::string, int>{{"maze", 1}});
    usage.reset_window(T0 + 30s);
    CHECK(usage.running());

    usage.end(SaverRunEnd::TOUCH, 0, T0 + 45s);
    const SaverUsageWindow second = usage.window(T0 + 50s);
    CHECK(second.shown_sec == std::map<std::string, int>{{"maze", 15}});
    CHECK(second.starts.empty());
    CHECK(second.woke_by_touch == std::map<std::string, int>{{"maze", 1}});

    usage.reset_window(T0 + 50s);
    CHECK(usage.window(T0 + 60s).empty());
}

TEST_CASE("SaverUsage carries sub-second remainders into the next window",
          "[screensaver][screensaver_usage]") {
    SaverUsage usage;
    usage.begin("maze", T0);
    usage.end(SaverRunEnd::OTHER, 0, T0 + 600ms);
    usage.begin("maze", T0 + 1s);
    usage.end(SaverRunEnd::OTHER, 0, T0 + 1s + 600ms);

    // Two runs of 0.6 s are one whole second, with 0.2 s left over.
    CHECK(usage.window(T0 + 2s).shown_sec == std::map<std::string, int>{{"maze", 1}});
    usage.reset_window(T0 + 2s);

    usage.begin("maze", T0 + 3s);
    usage.end(SaverRunEnd::OTHER, 0, T0 + 3s + 800ms);
    CHECK(usage.window(T0 + 4s).shown_sec == std::map<std::string, int>{{"maze", 1}});
}

TEST_CASE("SaverUsage: a window under a second shown reports no seconds",
          "[screensaver][screensaver_usage]") {
    SaverUsage usage;
    usage.begin("maze", T0);
    usage.end(SaverRunEnd::TOUCH, 0, T0 + 400ms);
    const SaverUsageWindow w = usage.window(T0 + 1s);
    CHECK(w.shown_sec.empty());
    CHECK(w.starts == std::map<std::string, int>{{"maze", 1}});
}
