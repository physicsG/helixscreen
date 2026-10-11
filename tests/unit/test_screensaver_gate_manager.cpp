// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#ifdef HELIX_ENABLE_SCREENSAVER

#include "../lvgl_test_fixture.h"
#include "../test_helpers/scoped_env.h"
#include "../test_helpers/screensaver_manager_test_access.h"
#include "../test_helpers/screensaver_test_access.h"
#include "config.h"
#include "helix_version.h"
#include "platform_capabilities.h"
#include "refresh_period_hold.h"
#include "screen_hide_hold.h"
#include "screensaver.h"
#include "screensaver_base.h"
#include "screensaver_gate.h"
#include "screensaver_level_store.h"

#include <cstdlib>
#include <optional>
#include <string>

#include "../catch_amalgamated.hpp"
#include "hv/json.hpp"

using helix::ScreensaverManagerTestAccess;
using helix::ui::CpuSample;
using helix::ui::SaverHost;
using helix::ui::SaverLevelEntry;

namespace {

constexpr const char* LEVELS_PATH = "/display/screensaver_levels";

/// Drives the shared manager's gate with a scripted CPU clock, and puts the clock, the host,
/// the stored levels and the gate environment back however the test ends.
class GateHarness {
  public:
    GateHarness() {
        mgr.stop();
        helix::Config* config = helix::Config::get_instance();
        if (const nlohmann::json* levels = config->try_get_json(LEVELS_PATH)) {
            saved_levels_ = *levels;
        }
        clear_levels();
        unsetenv("HELIX_SCREENSAVER_BUDGET_PCT");
        unsetenv("HELIX_SCREENSAVER_LEVEL");
        ScreensaverManagerTestAccess::set_cpu_clock(mgr,
                                                    [this] { return CpuSample{cpu_ns, wall_ns}; });
        ScreensaverManagerTestAccess::reset_baseline(mgr);
        mgr.set_host(SaverHost{[this] { return printing; }, "sdl"});
    }

    ~GateHarness() {
        mgr.stop();
        ScreensaverManagerTestAccess::set_cpu_clock(mgr, helix::ui::read_process_cpu_clock);
        ScreensaverManagerTestAccess::reset_baseline(mgr);
        mgr.set_host(SaverHost{});
        clear_levels();
        if (saved_levels_) {
            helix::Config::get_instance()->set<nlohmann::json>(LEVELS_PATH, *saved_levels_);
        }
    }

    GateHarness(const GateHarness&) = delete;
    GateHarness& operator=(const GateHarness&) = delete;

    /// Advances both clocks by `seconds` in 250 ms steps with the process using `cores` of CPU,
    /// calling the idle-check tick after each step.
    void run(double seconds, double cores) {
        constexpr uint64_t STEP_NS = 250000000ULL;
        const int steps = static_cast<int>(seconds * 4.0 + 0.5);
        for (int i = 0; i < steps; i++) {
            wall_ns += STEP_NS;
            cpu_ns += static_cast<uint64_t>(cores * static_cast<double>(STEP_NS));
            mgr.on_idle_check_tick();
        }
    }

    std::optional<SaverLevelEntry> stored(const char* name) const {
        return helix::ui::load_level_entry(*helix::Config::get_instance(), name);
    }

    void store(const char* name, const SaverLevelEntry& entry) {
        helix::ui::save_level_entry(*helix::Config::get_instance(), name, entry);
    }

    helix::ui::SaverBase* running() const {
        return ScreensaverManagerTestAccess::active(mgr);
    }

    ScreensaverManager& mgr = ScreensaverManager::instance();
    helix::ScopedEnv budget_env{"HELIX_SCREENSAVER_BUDGET_PCT"};
    helix::ScopedEnv level_env{"HELIX_SCREENSAVER_LEVEL"};
    uint64_t cpu_ns = 0;
    uint64_t wall_ns = 1000ULL * 1000000000ULL;
    bool printing = false;

  private:
    static void clear_levels() {
        helix::Config* config = helix::Config::get_instance();
        if (config->try_get_json(LEVELS_PATH) != nullptr) {
            config->get_json("/display").erase("screensaver_levels");
        }
    }

    std::optional<nlohmann::json> saved_levels_;
};

} // namespace

TEST_CASE_METHOD(LVGLTestFixture,
                 "the gate subtracts the idle baseline from a running saver's windows",
                 "[screensaver][screensaver_gate]") {
    GateHarness gate;
    setenv("HELIX_SCREENSAVER_BUDGET_PCT", "20", 1);
    gate.run(10.0, 0.05);
    CHECK(ScreensaverManagerTestAccess::baseline_rate(gate.mgr) ==
          Catch::Approx(0.05).margin(0.001));

    gate.mgr.start(ScreensaverType::FLYING_TOASTERS);
    REQUIRE(gate.running() != nullptr);
    REQUIRE(gate.running()->level() == 0);

    // 24% of a core with 5% of idle work is the saver using 19%, within the 20% budget.
    gate.run(6.25, 0.24);
    CHECK(gate.running()->level() == 0);
    CHECK_FALSE(gate.stored("toasters").has_value());

    // With no idle stretch behind it the baseline is 0, and the same load is over budget.
    gate.mgr.stop();
    gate.mgr.start(ScreensaverType::FLYING_TOASTERS);
    REQUIRE(gate.running() != nullptr);
    gate.run(6.25, 0.24);
    CHECK(gate.running()->level() == 1);
}

TEST_CASE_METHOD(
    LVGLTestFixture,
    "an over-budget saver steps down one level, stores it, and the next run starts there",
    "[screensaver][screensaver_gate]") {
    GateHarness gate;
    setenv("HELIX_SCREENSAVER_BUDGET_PCT", "20", 1);
    gate.run(10.0, 0.0);
    gate.mgr.start(ScreensaverType::FLYING_TOASTERS);
    REQUIRE(gate.running() != nullptr);

    gate.run(0.75, 0.9); // the warm-up second
    CHECK(gate.running()->level() == 0);
    gate.run(5.5, 0.5);
    REQUIRE(gate.running()->level() == 1);
    CHECK(SaverTestAccess::timer(*gate.running())->period == 33);
    const std::optional<SaverLevelEntry> entry = gate.stored("toasters");
    REQUIRE(entry.has_value());
    CHECK(entry->level == 1);
    CHECK_FALSE(entry->too_heavy);
    CHECK(entry->version == helix_version_full());
    CHECK(entry->board == ScreensaverManagerTestAccess::current_board(gate.mgr));

    // Far under budget from here on: a run never steps back up.
    gate.run(20.0, 0.01);
    CHECK(gate.running()->level() == 1);

    gate.mgr.stop();
    gate.mgr.start(ScreensaverType::FLYING_TOASTERS);
    REQUIRE(gate.running() != nullptr);
    CHECK(gate.running()->level() == 1);
}

TEST_CASE_METHOD(LVGLTestFixture, "the bouncing printer runs under the gate like every other saver",
                 "[screensaver][screensaver_gate]") {
    GateHarness gate;
    setenv("HELIX_SCREENSAVER_BUDGET_PCT", "20", 1);
    gate.run(10.0, 0.0);
    gate.mgr.start(ScreensaverType::BOUNCING_PRINTER);
    // The manager holds the bouncing printer in its gated saver list: active() is what
    // stayed nullptr while it ran off SaverBase.
    helix::ui::SaverBase* bounce = gate.running();
    REQUIRE(bounce != nullptr);
    CHECK(bounce->type() == ScreensaverType::BOUNCING_PRINTER);
    CHECK(bounce->level() == 0);

    gate.run(0.75, 0.9); // the warm-up second
    CHECK(gate.running()->level() == 0);
    gate.run(5.5, 0.5);
    REQUIRE(gate.running()->level() == 1);
    CHECK(SaverTestAccess::timer(*gate.running())->period == 33);
    const std::optional<SaverLevelEntry> entry = gate.stored("bounce");
    REQUIRE(entry.has_value());
    CHECK(entry->level == 1);
    CHECK_FALSE(entry->too_heavy);

    gate.mgr.stop();
    gate.mgr.start(ScreensaverType::BOUNCING_PRINTER);
    REQUIRE(gate.running() != nullptr);
    CHECK(gate.running()->level() == 1);
}

TEST_CASE_METHOD(
    LVGLTestFixture,
    "over budget at the lowest level stores the board as too heavy and shows a black screen",
    "[screensaver][screensaver_gate]") {
    GateHarness gate;
    setenv("HELIX_SCREENSAVER_BUDGET_PCT", "10", 1);
    gate.run(10.0, 0.0);
    gate.mgr.start(ScreensaverType::FLYING_TOASTERS);
    helix::ui::SaverBase* toasters = gate.running();
    REQUIRE(toasters != nullptr);

    // Walk to whatever the bottom rung is rather than naming it: the ladder's depth is the
    // saver's business, and hardcoding it here breaks every time a rung is added.
    const size_t bottom = toasters->level_count() - 1;
    for (size_t expected = 1; expected <= bottom; expected++) {
        gate.run(6.25, 0.9);
        CAPTURE(expected);
        REQUIRE(toasters->level() == expected);
    }
    // One more window over budget with nowhere left to step.
    gate.run(5.5, 0.9);

    CHECK(gate.running() == nullptr);
    CHECK_FALSE(toasters->is_active());
    CHECK(SaverTestAccess::timer(*toasters) == nullptr);
    CHECK(gate.mgr.is_active());
    REQUIRE(ScreensaverManagerTestAccess::showing_black_screen(gate.mgr));
    lv_obj_t* black = ScreensaverManagerTestAccess::black_screen(gate.mgr);
    REQUIRE(black != nullptr);
    CHECK(lv_obj_get_parent(black) == lv_layer_top());
    CHECK(lv_obj_get_style_bg_opa(black, LV_PART_MAIN) == LV_OPA_COVER);
    CHECK(helix::active_screen_hide_hold().is_held());
    CHECK_FALSE(helix::active_refresh_period_hold().is_held());
    const std::optional<SaverLevelEntry> entry = gate.stored("toasters");
    REQUIRE(entry.has_value());
    CHECK(entry->level == bottom);
    CHECK(entry->too_heavy);

    gate.mgr.stop();
    CHECK_FALSE(gate.mgr.is_active());
    CHECK_FALSE(helix::active_screen_hide_hold().is_held());

    // The next run goes straight to the black screen.
    gate.mgr.start(ScreensaverType::FLYING_TOASTERS);
    CHECK(gate.mgr.is_active());
    CHECK(gate.running() == nullptr);
    CHECK_FALSE(toasters->is_active());
    CHECK(ScreensaverManagerTestAccess::showing_black_screen(gate.mgr));
    CHECK(helix::active_screen_hide_hold().is_held());
}

TEST_CASE_METHOD(
    LVGLTestFixture,
    "a stored level from another app version or board, or a malformed one, starts at level 0",
    "[screensaver][screensaver_gate]") {
    GateHarness gate;
    const std::string board = ScreensaverManagerTestAccess::current_board(gate.mgr);
    const std::string version = helix_version_full();

    SECTION("the same version and board start at the stored level") {
        gate.store("toasters", {1, false, version, board});
        gate.mgr.start(ScreensaverType::FLYING_TOASTERS);
        REQUIRE(gate.running() != nullptr);
        CHECK(gate.running()->level() == 1);
    }
    SECTION("another app version") {
        gate.store("toasters", {1, true, "0.0.0 (other)", board});
        gate.mgr.start(ScreensaverType::FLYING_TOASTERS);
        REQUIRE(gate.running() != nullptr);
        CHECK(gate.running()->level() == 0);
        CHECK_FALSE(ScreensaverManagerTestAccess::showing_black_screen(gate.mgr));
    }
    SECTION("another board") {
        gate.store("toasters", {1, true, version, "fbdev/1c/0bm/1x1/16bpp"});
        gate.mgr.start(ScreensaverType::FLYING_TOASTERS);
        REQUIRE(gate.running() != nullptr);
        CHECK(gate.running()->level() == 0);
        CHECK_FALSE(ScreensaverManagerTestAccess::showing_black_screen(gate.mgr));
    }
    SECTION("a malformed entry") {
        helix::Config::get_instance()->set<nlohmann::json>("/display/screensaver_levels/toasters",
                                                           nlohmann::json("level one"));
        gate.mgr.start(ScreensaverType::FLYING_TOASTERS);
        REQUIRE(gate.running() != nullptr);
        CHECK(gate.running()->level() == 0);
    }
}

TEST_CASE_METHOD(
    LVGLTestFixture,
    "HELIX_SCREENSAVER_LEVEL forces a level with the gate off, and past the ladder is ignored",
    "[screensaver][screensaver_gate]") {
    GateHarness gate;
    setenv("HELIX_SCREENSAVER_BUDGET_PCT", "1", 1);
    setenv("HELIX_SCREENSAVER_LEVEL", "1", 1);
    gate.run(10.0, 0.0);
    gate.mgr.start(ScreensaverType::FLYING_TOASTERS);
    REQUIRE(gate.running() != nullptr);
    CHECK(gate.running()->level() == 1);

    gate.run(30.0, 1.0);
    CHECK(gate.running() != nullptr);
    CHECK_FALSE(ScreensaverManagerTestAccess::showing_black_screen(gate.mgr));
    CHECK_FALSE(gate.stored("toasters").has_value());

    gate.mgr.stop();
    unsetenv("HELIX_SCREENSAVER_BUDGET_PCT");
    setenv("HELIX_SCREENSAVER_LEVEL", "7", 1);
    gate.mgr.start(ScreensaverType::FLYING_TOASTERS);
    REQUIRE(gate.running() != nullptr);
    // Toasters have levels 0 to 2; a clamp would give 2, ignoring gives the fresh level 0.
    CHECK(gate.running()->level() == 0);
}

TEST_CASE_METHOD(LVGLTestFixture,
                 "the budget follows the core count and halves when a print starts mid-run",
                 "[screensaver][screensaver_gate]") {
    GateHarness gate;
    const double full =
        helix::ui::saver_budget_share(helix::PlatformCapabilities::detect().cpu_cores, false);
    const double load = full * 0.75;
    gate.run(10.0, 0.0);
    gate.mgr.start(ScreensaverType::FLYING_TOASTERS);
    REQUIRE(gate.running() != nullptr);

    gate.run(6.25, load);
    CHECK(gate.running()->level() == 0);

    gate.printing = true;
    gate.run(5.0, load);
    CHECK(gate.running()->level() == 1);
}

#endif // HELIX_ENABLE_SCREENSAVER

TEST_CASE_METHOD(LVGLTestFixture,
                 "every saver the registry marks for this colour depth is registered",
                 "[screensaver][screensaver_gate][depth_wiring]") {
    // Four hand-kept lists decide this: SCREENSAVERS::depths, the Makefile source filter, and
    // the manager's include and registration guards. A saver missing here is selectable and
    // starts nothing.
    const std::vector<const char*> missing =
        ScreensaverManager::instance().savers_missing_for_build_depth();
    std::string names;
    for (const char* n : missing) {
        names += names.empty() ? n : std::string(", ") + n;
    }
    CAPTURE(names);
    CHECK(missing.empty());
}
