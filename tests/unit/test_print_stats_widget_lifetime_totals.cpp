// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_print_stats_widget_lifetime_totals.cpp
 * @brief The print stats tile's lifetime line reports the SERVER's totals, not the sum
 *        of the capped local job cache (#1272), and the tile asks the history cache to
 *        cover this week's bars.
 *
 * A COMPLETE load pulls at most PrintHistoryManager::kCompleteJobLimit jobs.
 * Summing that cache silently truncates every printer with a longer history: the
 * reporter saw 500 prints / 209h next to Mainsail's 764.
 *
 * The cap is reproduced here in miniature: a whole-list load that came back
 * exactly full at three jobs, against a mock whose history holds more, so the
 * cache sum and the server totals disagree the way they do at 500.
 */

#include "../../include/app_globals.h"
#include "../../include/moonraker_api.h"
#include "../../include/moonraker_client_mock.h"
#include "../../include/panel_widget_manager.h"
#include "../../include/print_history_data.h"
#include "../../include/print_history_manager.h"
#include "../../include/printer_state.h"
#include "../lvgl_ui_test_fixture.h"
#include "../test_helpers/history_call_counting_api.h"
#include "../test_helpers/panel_widget_size_harness.h"
#include "../test_helpers/planted_gcode.h"
#include "../test_helpers/print_history_manager_test_access.h"
#include "print_activity.h"
#include "src/ui/panel_widgets/print_stats_widget.h"

#include <atomic>
#include <cstdio>
#include <ctime>
#include <memory>
#include <string>

#include "../catch_amalgamated.hpp"

using namespace helix;

namespace {

const char* subject_text(const char* name) {
    lv_subject_t* subject = lv_xml_get_subject(nullptr, name);
    REQUIRE(subject != nullptr);
    return lv_subject_get_string(subject);
}

/// A box that holds the Wide rung, where the full lifetime line is drawn.
constexpr int kWideW = 236;
constexpr int kWideH = 95;

/// The lifetime line the tile composes for @p totals at Wide and up.
std::string lifetime_line(const PrintHistoryTotals& totals) {
    return format_lifetime_line(totals, true);
}

class PrintStatsLifetimeFixture : public LVGLUITestFixture {
  public:
    PrintStatsLifetimeFixture() : client_(MoonrakerClientMock::PrinterType::VORON_24, 1000.0) {
        client_.connect("ws://mock/websocket", []() {}, []() {});
        api_ = std::make_unique<helix::HistoryCallCountingMoonrakerAPI>(client_, state());
        manager_ = std::make_unique<PrintHistoryManager>(api_.get(), &client_);

        set_moonraker_api(api_.get());
        set_print_history_manager(manager_.get());

        // Widget-owned subjects register lazily.
        PanelWidgetManager::instance().init_widget_subjects();
    }

    ~PrintStatsLifetimeFixture() override {
        set_print_history_manager(nullptr);
        set_moonraker_api(nullptr);
        manager_.reset();
        api_.reset();
        client_.disconnect();
    }

  protected:
    /// Read the server-computed totals directly — the same numbers Mainsail shows.
    PrintHistoryTotals fetch_server_totals() {
        PrintHistoryTotals totals;
        std::atomic<bool> done{false};
        api_->history().get_history_totals(
            [&](const PrintHistoryTotals& t) {
                totals = t;
                done = true;
            },
            [&](const MoonrakerError&) { done = true; });
        REQUIRE(wait_until([&]() { return done.load(); }));
        return totals;
    }

    /// Install a whole-list load that came back exactly full: the printer has
    /// more jobs than the response returned, which is what a history longer
    /// than kCompleteJobLimit looks like to the cache.
    void load_capped_cache(size_t n) {
        manager_->fetch(HistoryScope::COMPLETE);
        REQUIRE(wait_until([&]() { return manager_->is_loaded(HistoryScope::RECENT); }));
        REQUIRE(manager_->get_jobs().size() > n);
        std::vector<PrintHistoryJob> capped(manager_->get_jobs().begin(),
                                            manager_->get_jobs().begin() +
                                                static_cast<std::ptrdiff_t>(n));
        helix::PrintHistoryManagerTestAccess::set_loaded_jobs(
            *manager_, std::move(capped), HistoryScope::COMPLETE, static_cast<int>(n));
    }

    /// The real tile tree for @p widget, sized to the Wide box and attached.
    lv_obj_t* attach_wide_tile(PrintStatsWidget& widget) {
        lv_obj_t* tile = static_cast<lv_obj_t*>(
            lv_xml_create(test_screen(), "panel_widget_print_stats", nullptr));
        REQUIRE(tile != nullptr);
        lv_obj_set_size(tile, kWideW, kWideH);
        lv_obj_update_layout(tile);
        widget.attach(tile, test_screen());
        widget.notify_size_changed(1, 1, kWideW, kWideH);
        return tile;
    }

    MoonrakerClientMock client_;
    std::unique_ptr<helix::HistoryCallCountingMoonrakerAPI> api_;
    std::unique_ptr<PrintHistoryManager> manager_;
};

} // namespace

// The mock derives its totals from the shipped gcodes directory, and other
// test processes plant files for their own cases while these run.
TEST_CASE_METHOD(PrintStatsLifetimeFixture,
                 "A planted G-code never reaches the mock's history totals",
                 "[print_stats][history][1272]") {
    const PrintHistoryTotals before = fetch_server_totals();
    PlantedGcode planted("history_totals_planted.gcode");
    REQUIRE(planted.on_disk());
    CHECK(fetch_server_totals().total_jobs == before.total_jobs);
}

TEST_CASE_METHOD(PrintStatsLifetimeFixture,
                 "print_stats lifetime totals come from the server, not the capped job cache",
                 "[print_stats][history][1272]") {
    PrintHistoryTotals server = fetch_server_totals();
    REQUIRE(server.total_jobs > 3); // otherwise the cap below can't bite

    // Cache capped below the real history size — same shape as >500 jobs.
    load_capped_cache(3);
    REQUIRE(manager_->get_jobs().size() == 3);

    uint64_t cached_time = 0;
    for (const auto& job : manager_->get_jobs()) {
        cached_time += static_cast<uint64_t>(job.total_duration);
    }
    REQUIRE(cached_time < server.total_time);

    PrintStatsWidget widget;
    lv_obj_t* tile = attach_wide_tile(widget);
    widget.on_activate();

    const std::string expected = lifetime_line(server);
    const std::string from_cache =
        format_lifetime_line({manager_->get_jobs().size(), cached_time, 0.0}, true);
    REQUIRE(expected != from_cache);

    bool settled =
        wait_until([&]() { return expected == subject_text("print_stats_support_text"); });

    INFO("support_text=" << subject_text("print_stats_support_text") << " (cache holds "
                         << manager_->get_jobs().size() << " jobs / " << cached_time << "s)");
    CHECK(settled);
    CHECK(expected == std::string(subject_text("print_stats_support_text")));

    widget.detach();
    lv_obj_delete(tile);
}

TEST_CASE_METHOD(PrintStatsLifetimeFixture,
                 "print_stats refetches lifetime totals after a grid rebuild recycles it",
                 "[print_stats][history][1272]") {
    PrintHistoryTotals server = fetch_server_totals();
    REQUIRE(server.total_jobs > 3);

    load_capped_cache(3);

    PrintStatsWidget widget;

    // First attach: activate, then tear down before the in-flight totals reply
    // is drained, exactly as a grid rebuild does. The reply is dropped by the
    // lifetime guard — nothing may stay latched that blocks the next request.
    lv_obj_t* first = attach_wide_tile(widget);
    widget.on_activate();
    widget.detach();
    lv_obj_delete(first);

    // Same C++ instance re-attached into the rebuilt grid (supports_reuse()).
    lv_obj_t* second = attach_wide_tile(widget);
    widget.on_activate();

    const std::string expected = lifetime_line(server);
    bool settled =
        wait_until([&]() { return expected == subject_text("print_stats_support_text"); });
    INFO("support_text=" << subject_text("print_stats_support_text"));
    CHECK(settled);

    widget.detach();
    lv_obj_delete(second);
}

TEST_CASE_METHOD(PrintStatsLifetimeFixture,
                 "print_stats escalates when the cached slice stops short of a week",
                 "[print_stats][history][coverage]") {
    // The seven bars reach back to the oldest local midnight. A startup slice that
    // filled its page inside a few days leaves the rest of that week on the printer,
    // so activating has to pull the whole list.
    const double now = static_cast<double>(std::time(nullptr));
    std::vector<PrintHistoryJob> page;
    for (int i = 0; i < PrintHistoryManager::kRecentJobLimit; ++i) {
        PrintHistoryJob job;
        job.job_id = "job" + std::to_string(i);
        job.filename = "a.gcode";
        job.status = PrintJobStatus::COMPLETED;
        job.start_time = now - static_cast<double>(i) * 3600; // one an hour
        page.push_back(job);
    }
    // The staleness watcher is queued with the socket down and stales any cache
    // it finds when it runs, so drain it and bring the socket up first.
    process_lvgl(20);
    lv_subject_set_int(state().network_state().get_printer_connection_state_subject(),
                       static_cast<int>(ConnectionState::CONNECTED));
    process_lvgl(20);
    helix::PrintHistoryManagerTestAccess::set_loaded_jobs(*manager_, page, HistoryScope::RECENT,
                                                          PrintHistoryManager::kRecentJobLimit);
    const double window_start = local_day_starts(static_cast<std::time_t>(now))[0];
    REQUIRE_FALSE(manager_->covers_since(window_start));

    PrintStatsWidget widget;
    lv_obj_t* obj = lv_obj_create(test_screen());
    widget.attach(obj, test_screen());
    widget.on_activate();

    // Assert on the request, not on the cache: this mock's history is short
    // enough that any load at all would leave the cache complete, so a
    // cache-shaped assertion could not tell the escalation from a no-op.
    REQUIRE(wait_until(
        [&]() { return api_->history_last_limit() == PrintHistoryManager::kCompleteJobLimit; }));

    widget.detach();
    lv_obj_delete(obj);
}
