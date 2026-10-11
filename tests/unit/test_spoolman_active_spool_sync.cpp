// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_spoolman_active_spool_sync.cpp
 * @brief Moonraker's global active spool, mirrored onto the bypass slot and the
 *        active toolchanger tool.
 *
 * Moonraker has ONE active spool and an AMS lane assignment sets it too, so the
 * notification that comes back can describe a lane. The four things pinned here:
 * the bypass gate lets a no-AMS printer's spool through, a lane's spool never
 * overwrites the bypass record, a toolchanger tool adopts the spool even though no
 * toolchanger ever passes the bypass gate, and a status that already matches the
 * external slot costs no fetch.
 */

#include "ui_update_queue.h"

#include "../fake_moonraker_client.h"
#include "../helix_test_fixture.h"
#include "../lvgl_test_fixture.h"
#include "../test_helpers/unique_temp_dir.h"
#include "../test_helpers/update_queue_test_access.h"
#include "ams_backend_mock.h"
#include "ams_backend_toolchanger.h"
#include "ams_state.h"
#include "app_constants.h"
#include "config.h"
#include "i_moonraker_sub_apis.h"
#include "spoolman_active_spool_sync.h"
#include "spoolman_types.h"
#include "tool_state.h"

#include <filesystem>
#include <memory>
#include <vector>

#include "../catch_amalgamated.hpp"

using helix::AmsBackendMock;
using helix::AmsBackendToolChanger;
using helix::AmsState;
using helix::Config;
using helix::SlotInfo;
using helix::test::FakeMoonrakerClient;
using nlohmann::json;
namespace fs = std::filesystem;
namespace spool_sync = helix::spoolman_sync;

namespace {

/// Records the Spoolman calls the sync makes and keeps their callbacks so a test
/// decides when, and with what, the server answers.
class RecordingSpoolman : public ISpoolmanAPI {
  public:
    struct SpoolRequest {
        int id;
        helix::SpoolCallback on_success;
    };
    std::vector<SpoolRequest> spool_requests;
    int status_requests = 0;

    void get_spoolman_status(std::function<void(bool, int)>, ErrorCallback, bool) override {
        ++status_requests;
    }
    void get_spoolman_spool(int spool_id, helix::SpoolCallback on_success, ErrorCallback,
                            bool) override {
        spool_requests.push_back({spool_id, std::move(on_success)});
    }

    /// Answer the oldest outstanding fetch with a spool of that id.
    void answer_with_spool(size_t index, int id) {
        SpoolInfo spool;
        spool.id = id;
        spool.vendor = "Polymaker";
        spool.material = "PLA";
        spool.filament_name = "Jet Black";
        spool.remaining_weight_g = 800;
        spool.initial_weight_g = 1000;
        spool_requests.at(index).on_success(spool);
    }

    void get_spoolman_spools(helix::SpoolListCallback, ErrorCallback) override {}
    void set_active_spool(int, SuccessCallback, ErrorCallback) override {}
    void get_spool_usage_history(int, std::function<void(const std::vector<FilamentUsageRecord>&)>,
                                 ErrorCallback) override {}
    void update_spoolman_spool_weight(int, double, SuccessCallback, ErrorCallback) override {}
    void update_spoolman_spool(int, const json&, SuccessCallback, ErrorCallback) override {}
    void update_spoolman_filament(int, const json&, SuccessCallback, ErrorCallback) override {}
    void update_spoolman_filament_color(int, const std::string&, SuccessCallback,
                                        ErrorCallback) override {}
    void get_spoolman_vendors(helix::VendorListCallback, ErrorCallback) override {}
    void get_spoolman_filaments(helix::FilamentListCallback, ErrorCallback) override {}
    void get_spoolman_filaments(int, helix::FilamentListCallback, ErrorCallback) override {}
    void create_spoolman_vendor(const json&, helix::VendorCreateCallback, ErrorCallback) override {}
    void create_spoolman_filament(const json&, helix::FilamentCreateCallback,
                                  ErrorCallback) override {}
    void create_spoolman_spool(const json&, helix::SpoolCreateCallback, ErrorCallback) override {}
    void delete_spoolman_spool(int, SuccessCallback, ErrorCallback) override {}
    void delete_spoolman_vendor(int, SuccessCallback, ErrorCallback) override {}
    void delete_spoolman_filament(int, SuccessCallback, ErrorCallback) override {}
    void search_spoolman_external_filaments(const std::string&, int,
                                            helix::ExternalFilamentListCallback,
                                            ErrorCallback) override {}
};

struct SyncFixture : LVGLTestFixture {
    std::string temp_dir = helix::test::unique_temp_dir("helix_spool_sync");
    FakeMoonrakerClient client;
    RecordingSpoolman spoolman;

    SyncFixture() {
        fs::create_directories(temp_dir);
        fs::remove(AppConstants::Update::config_backup_fallback());
        fs::remove(AppConstants::Update::legacy_config_backup_fallback());
        fs::remove(AppConstants::Update::env_backup_fallback());
        Config::get_instance()->init(temp_dir + "/settings.json");
        AmsState::instance().clear_backends();
        AmsState::instance().clear_external_spool_info();
        spool_sync::attach(client, spoolman);
    }

    ~SyncFixture() override {
        spool_sync::detach(client);
        helix::ui::UpdateQueueTestAccess::drain(helix::ui::UpdateQueue::instance());
        AmsState::instance().clear_backends();
        AmsState::instance().clear_external_spool_info();
        Config::get_instance()->clear_path();
        std::error_code ec;
        fs::remove_all(temp_dir, ec);
    }

    void notify_active_spool(int id) {
        REQUIRE(client.fire_notification("notify_active_spool_set",
                                         json{{"params", json::array({json{{"spool_id", id}}})}}));
    }

    void pump() {
        helix::ui::UpdateQueueTestAccess::drain(helix::ui::UpdateQueue::instance());
    }

    static int external_spool_id() {
        auto info = AmsState::instance().get_external_spool_info();
        return info ? info->spoolman_id : 0;
    }
};

} // namespace

TEST_CASE_METHOD(SyncFixture, "attach syncs once, subscribes once, and detach drops it",
                 "[spoolman_sync]") {
    CHECK(spoolman.status_requests == 1);
    CHECK(client.handler_count("notify_active_spool_set", "external_spool_sync") == 1);

    spool_sync::attach(client, spoolman);
    CHECK(spoolman.status_requests == 2);
    CHECK(client.handler_count("notify_active_spool_set", "external_spool_sync") == 1);

    spool_sync::detach(client);
    CHECK(client.handler_count("notify_active_spool_set", "external_spool_sync") == 0);
}

TEST_CASE_METHOD(SyncFixture, "a printer with no AMS hands the active spool to the bypass slot",
                 "[spoolman_sync]") {
    REQUIRE(AmsState::instance().active_spool_describes_bypass());

    notify_active_spool(5);
    REQUIRE(spoolman.spool_requests.size() == 1);
    CHECK(spoolman.spool_requests[0].id == 5);
    spoolman.answer_with_spool(0, 5);
    pump();

    CHECK(external_spool_id() == 5);
}

TEST_CASE_METHOD(SyncFixture, "a lane's spool does not overwrite the bypass record",
                 "[spoolman_sync]") {
    auto mock = std::make_unique<AmsBackendMock>(4);
    mock->set_operation_delay(0);
    AmsState::instance().set_backend(std::move(mock));
    REQUIRE_FALSE(AmsState::instance().active_spool_describes_bypass());
    SlotInfo bypass;
    bypass.spoolman_id = 9;
    bypass.material = "PETG";
    AmsState::instance().set_external_spool_info(bypass);
    REQUIRE(external_spool_id() == 9);

    SECTION("a lane's spool arrives") {
        notify_active_spool(5);
        REQUIRE(spoolman.spool_requests.size() == 1);
        spoolman.answer_with_spool(0, 5);
        pump();
        CHECK(external_spool_id() == 9);
    }

    SECTION("clearing a lane comes back as spool 0") {
        notify_active_spool(0);
        pump();
        CHECK(spoolman.spool_requests.empty());
        CHECK(external_spool_id() == 9);
    }
}

TEST_CASE_METHOD(SyncFixture,
                 "clearing the active spool clears the bypass when nothing else owns it",
                 "[spoolman_sync]") {
    SlotInfo bypass;
    bypass.spoolman_id = 9;
    bypass.material = "PETG";
    AmsState::instance().set_external_spool_info(bypass);
    REQUIRE(AmsState::instance().active_spool_describes_bypass());

    notify_active_spool(0);
    pump();

    CHECK(external_spool_id() == 0);
}

TEST_CASE_METHOD(SyncFixture, "a toolchanger tool with no spool adopts the active one",
                 "[spoolman_sync][toolchanger]") {
    auto& tools = helix::ToolState::instance();
    tools.deinit_subjects();
    tools.init_subjects(false);
    tools.set_config_dir(temp_dir);
    helix::PrinterDiscovery hw;
    hw.parse_objects(json::array({"extruder", "extruder1", "heater_bed"}));
    tools.init_tools(hw);
    tools.load_spool_assignments(nullptr);
    REQUIRE(tools.spool_assignments_loaded());
    REQUIRE(tools.tools()[0].spoolman_id == 0);

    auto changer = std::make_unique<AmsBackendToolChanger>(nullptr, nullptr);
    changer->set_discovered_tools({"T0", "T1"});
    AmsState::instance().set_backend(std::move(changer));
    AmsState::instance().init_subjects(true);
    AmsState::instance().sync_from_backend();
    REQUIRE(tools.tools().size() == 2);
    REQUIRE(tools.tools()[0].spoolman_id == 0);

    // The bare backend reports no mounted tool; mount T0 on the same topology so the
    // tool list is not rebuilt out from under the assignment.
    helix::ToolTopology mounted;
    mounted.tool_count = 2;
    mounted.active_tool = 0;
    mounted.tool_to_slot = {tools.tools()[0].backend_slot, tools.tools()[1].backend_slot};
    mounted.backend_index = tools.tools()[0].backend_index;
    tools.set_ams_topology(mounted);
    REQUIRE(tools.active_tool_index() == 0);
    // A changer never reads as bypass, so the bypass gate cannot be what admits this assign.
    REQUIRE_FALSE(AmsState::instance().active_spool_describes_bypass());

    notify_active_spool(42);
    REQUIRE(spoolman.spool_requests.size() == 1);
    spoolman.answer_with_spool(0, 42);
    pump();

    REQUIRE(tools.tools().size() == 2);
    CHECK(tools.tools()[0].spoolman_id == 42);
    CHECK(external_spool_id() == 0);

    // Already assigned: a different active spool does not displace it.
    notify_active_spool(43);
    spoolman.answer_with_spool(1, 43);
    pump();
    CHECK(tools.tools()[0].spoolman_id == 42);

    tools.deinit_subjects();
}

TEST_CASE_METHOD(SyncFixture, "an AMS lane's active spool is not adopted by a tool",
                 "[spoolman_sync][toolchanger]") {
    auto& tools = helix::ToolState::instance();
    tools.deinit_subjects();
    tools.init_subjects(false);
    tools.set_config_dir(temp_dir);
    helix::PrinterDiscovery hw;
    hw.parse_objects(json::array({"extruder", "extruder1", "heater_bed"}));
    tools.init_tools(hw);
    tools.load_spool_assignments(nullptr);
    auto mock = std::make_unique<AmsBackendMock>(4);
    mock->set_operation_delay(0);
    AmsState::instance().set_backend(std::move(mock));

    notify_active_spool(42);
    spoolman.answer_with_spool(0, 42);
    pump();

    CHECK(tools.tools()[0].spoolman_id == 0);
    tools.deinit_subjects();
}

TEST_CASE_METHOD(SyncFixture, "a status that already matches the external slot costs no fetch",
                 "[spoolman_sync]") {
    SlotInfo bypass;
    bypass.spoolman_id = 7;
    bypass.material = "PLA";
    AmsState::instance().set_external_spool_info(bypass);

    SECTION("the same spool") {
        spool_sync::sync_from_status(spoolman, true, 7);
        CHECK(spoolman.spool_requests.empty());
    }
    SECTION("Spoolman is down") {
        spool_sync::sync_from_status(spoolman, false, 8);
        CHECK(spoolman.spool_requests.empty());
    }
    SECTION("no spool is active") {
        spool_sync::sync_from_status(spoolman, true, 0);
        CHECK(spoolman.spool_requests.empty());
    }
    SECTION("a different spool is fetched") {
        spool_sync::sync_from_status(spoolman, true, 8);
        REQUIRE(spoolman.spool_requests.size() == 1);
        CHECK(spoolman.spool_requests[0].id == 8);
    }
}
