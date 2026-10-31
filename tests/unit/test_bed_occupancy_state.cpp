// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../lvgl_test_fixture.h"
#include "../test_helpers/bed_occupancy_state_test_access.h"
#include "bed_occupancy_state.h"
#include "config.h"
#include "connection_state.h"
#include "moonraker_api.h"
#include "moonraker_client_mock.h"
#include "print_history_manager.h"
#include "printer_excluded_objects_state.h"
#include "printer_network_state.h"
#include "printer_state.h"

#include <memory>
#include <string>
#include <vector>

#include "../catch_amalgamated.hpp"

using namespace helix;

namespace {

class BedOccupancyStateFixture : public LVGLTestFixture {
  public:
    BedOccupancyStateFixture() : client_(MoonrakerClientMock::PrinterType::VORON_24, 1000.0) {
        printer_state_.init_subjects(false);
        client_.connect("ws://mock/websocket", []() {}, []() {});
        api_ = std::make_unique<MoonrakerAPI>(client_, printer_state_);
        history_ = std::make_unique<PrintHistoryManager>(api_.get(), &client_);
    }

    ~BedOccupancyStateFixture() override {
        state_.reset();
        history_.reset();
        api_.reset();
        client_.disconnect();
        ui::UpdateQueue::instance().drain();
    }

    /// A state over the fixture's history, with the socket up so it loads history itself.
    void make_state() {
        state_ = std::make_unique<BedOccupancyState>(printer_state_, history_.get());
        state_->init_subjects();
        lv_subject_set_int(printer_state_.network_state().get_printer_connection_state_subject(),
                           static_cast<int>(ConnectionState::CONNECTED));
        REQUIRE(wait_until([&] { return !history_->get_jobs().empty(); }));
        ui::UpdateQueue::instance().drain();
    }

    /// A fresh history manager and state, connected, whose first RECENT load either stays
    /// unanswered (@p fail false) or fails (@p fail true).
    void restart_with_unloaded_history(bool fail) {
        state_.reset();
        history_ = std::make_unique<PrintHistoryManager>(api_.get(), &client_);
        if (fail) {
            client_.fail_next("server.history.list");
        } else {
            client_.defer_next("server.history.list");
        }
        const int calls = client_.call_count("server.history.list");
        state_ = std::make_unique<BedOccupancyState>(printer_state_, history_.get());
        // The socket is still up, so the connection observer's first call asks for history.
        state_->init_subjects();
        REQUIRE(wait_until([&] { return client_.call_count("server.history.list") > calls; }));
        ui::UpdateQueue::instance().drain();
        REQUIRE_FALSE(history_->is_loaded(HistoryScope::RECENT));
    }

    int occupied() {
        return lv_subject_get_int(state_->get_occupied_subject());
    }

    const PrintHistoryJob& newest() const {
        return history_->get_jobs().front();
    }

    void frame(const nlohmann::json& status) {
        printer_state_.update_from_status(status);
        ui::UpdateQueue::instance().drain();
    }

    void print_stats(const char* state) {
        frame({{"print_stats", {{"state", state}, {"filename", newest().filename}}}});
    }

    MoonrakerClientMock client_;
    PrinterState printer_state_;
    std::unique_ptr<MoonrakerAPI> api_;
    std::unique_ptr<PrintHistoryManager> history_;
    std::unique_ptr<BedOccupancyState> state_;
};

PrinterExcludedObjectsState::ObjectInfo square(const char* name, float x0, float y0, float size) {
    return PrinterExcludedObjectsState::make_object_info(
        name, glm::vec2{x0 + size / 2, y0 + size / 2},
        {{x0, y0}, {x0 + size, y0}, {x0 + size, y0 + size}, {x0, y0 + size}});
}

} // namespace

TEST_CASE_METHOD(BedOccupancyStateFixture,
                 "the newest completed job occupies the bed at its object height",
                 "[bed_occupancy][state]") {
    make_state();
    REQUIRE(newest().status == PrintJobStatus::COMPLETED);
    REQUIRE(newest().object_height > 0.0);

    CHECK(occupied() == 1);
    REQUIRE(state_->current().part_height_mm.has_value());
    CHECK(*state_->current().part_height_mm == Catch::Approx(newest().object_height));
    CHECK(state_->current().job_id == newest().job_id);
}

TEST_CASE_METHOD(BedOccupancyStateFixture, "Cleared persists across a new state over the config",
                 "[bed_occupancy][state]") {
    make_state();
    REQUIRE(occupied() == 1);
    const int version = lv_subject_get_int(state_->get_version_subject());

    state_->mark_cleared();
    CHECK(occupied() == 0);
    CHECK_FALSE(state_->current().occupied);
    CHECK(lv_subject_get_int(state_->get_version_subject()) > version);

    state_.reset();
    make_state();
    CHECK(occupied() == 0);
}

TEST_CASE_METHOD(BedOccupancyStateFixture, "a print starting clears the bed",
                 "[bed_occupancy][state]") {
    make_state();
    REQUIRE(occupied() == 1);

    print_stats("printing");
    CHECK(occupied() == 0);
    CHECK(state_->outlines().empty());
}

TEST_CASE_METHOD(BedOccupancyStateFixture,
                 "outlines outlive the exclude-object geometry once a print ends",
                 "[bed_occupancy][state]") {
    make_state();
    print_stats("printing");
    printer_state_.excluded_objects_state().set_defined_objects_with_geometry(
        {square("a", 100, 100, 50), square("b", 200, 50, 20)});
    ui::UpdateQueue::instance().drain();
    REQUIRE(state_->outlines().size() == 2);

    print_stats("complete");
    REQUIRE(occupied() == 1);
    printer_state_.excluded_objects_state().clear_objects();
    ui::UpdateQueue::instance().drain();
    REQUIRE(printer_state_.excluded_objects_state().get_defined_objects().empty());

    const auto outlines = state_->outlines();
    REQUIRE(outlines.size() == 2);
    REQUIRE(outlines[0].points.size() == 4);
    CHECK(outlines[0].points[2].x == Catch::Approx(150.0f));
    CHECK(outlines[0].points[2].y == Catch::Approx(150.0f));

    // A restart of the app brings the snapshot back from the config.
    state_.reset();
    make_state();
    CHECK(state_->outlines().size() == 2);
}

TEST_CASE_METHOD(BedOccupancyStateFixture, "a snapshot of another file draws no outlines",
                 "[bed_occupancy][state]") {
    BedOutlineSnapshot other;
    other.filename = "not-the-newest-job.gcode";
    other.objects.push_back({{{10, 10}, {20, 10}, {20, 20}}});
    Config* config = Config::get_instance();
    config->set<std::string>(config->df() + kBedOutlineSnapshotKey,
                             bed_outline_snapshot_to_json(other));

    make_state();
    REQUIRE(occupied() == 1);
    REQUIRE(newest().filename != other.filename);
    CHECK(state_->outlines().empty());
}

TEST_CASE_METHOD(BedOccupancyStateFixture,
                 "a restart keeps the part on the bed until history has loaded",
                 "[bed_occupancy][state]") {
    make_state();
    REQUIRE(occupied() == 1);
    const double height = newest().object_height;
    const std::string job_id = newest().job_id;

    restart_with_unloaded_history(/*fail=*/false);
    CHECK(occupied() == 1);
    CHECK(state_->current().job_id == job_id);
    REQUIRE(state_->current().part_height_mm.has_value());
    CHECK(*state_->current().part_height_mm == Catch::Approx(height));
    client_.fire_deferred("server.history.list");
}

TEST_CASE_METHOD(BedOccupancyStateFixture,
                 "a failed history load keeps the part on the bed and is retried",
                 "[bed_occupancy][state]") {
    make_state();
    REQUIRE(occupied() == 1);
    const double height = newest().object_height;

    restart_with_unloaded_history(/*fail=*/true);
    CHECK(occupied() == 1);
    REQUIRE(state_->current().part_height_mm.has_value());
    CHECK(*state_->current().part_height_mm == Catch::Approx(height));

    const int calls = client_.call_count("server.history.list");
    REQUIRE(BedOccupancyStateTestAccess::fire_history_retry(*state_));
    REQUIRE(wait_until([&] { return history_->is_loaded(HistoryScope::RECENT); }));
    CHECK(client_.call_count("server.history.list") > calls);
    ui::UpdateQueue::instance().drain();
    CHECK(occupied() == 1);
    CHECK_FALSE(BedOccupancyStateTestAccess::history_retry_armed(*state_));
}

TEST_CASE_METHOD(BedOccupancyStateFixture, "a cleared bed records no part for the next restart",
                 "[bed_occupancy][state]") {
    make_state();
    REQUIRE(occupied() == 1);
    state_->mark_cleared();
    REQUIRE(occupied() == 0);

    restart_with_unloaded_history(/*fail=*/false);
    CHECK(occupied() == 0);
    client_.fire_deferred("server.history.list");
}

TEST_CASE_METHOD(BedOccupancyStateFixture,
                 "a print starting or ending bumps the version even when the verdict holds",
                 "[bed_occupancy][state]") {
    make_state();
    state_->mark_cleared();
    REQUIRE(occupied() == 0);

    const int before = lv_subject_get_int(state_->get_version_subject());
    print_stats("printing");
    REQUIRE(occupied() == 0);
    CHECK(lv_subject_get_int(state_->get_version_subject()) > before);
}
