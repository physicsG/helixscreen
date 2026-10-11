// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Connection page: its three rows carry their callbacks, and the Host row
// shows the printer address from config. The XML names its callbacks by string
// and silently skips one that is not registered, so a row whose callback went
// missing stays on screen and does nothing.

#include "ui_panel_settings.h"
#include "ui_settings_connection.h"
#include "ui_update_queue.h"

#include "../lvgl_ui_test_fixture.h"
#include "config.h"
#include "helix-xml/src/xml/lv_xml.h"
#include "settings_manager.h"

#include <string>

#include "../catch_amalgamated.hpp"

namespace {

struct ConnectionCallbacksFixture : LVGLUITestFixture {
    lv_obj_t* root_ = nullptr;
    lv_obj_t* panel_stub_ = nullptr;
    helix::Config* config_ = helix::Config::get_instance();
    std::string saved_host_;
    int saved_port_ = 0;

    ConnectionCallbacksFixture() {
        const std::string key = config_->df();
        saved_host_ = config_->get<std::string>(key + "moonraker_host", "");
        saved_port_ = config_->get<int>(key + "moonraker_port", 7125);
        config_->set<std::string>(key + "moonraker_host", "10.0.0.7");
        config_->set<int>(key + "moonraker_port", 7125);

        SettingsManager::instance().init_subjects();
        auto& panel = get_global_settings_panel();
        panel.init_subjects();
        panel_stub_ = lv_obj_create(test_screen());
        panel.setup(panel_stub_, test_screen());

        auto& overlay = helix::settings::get_connection_settings_overlay();
        overlay.init_subjects();
        overlay.register_callbacks();
        root_ = static_cast<lv_obj_t*>(
            lv_xml_create(test_screen(), "settings_connection_overlay", nullptr));
        REQUIRE(root_ != nullptr);
        overlay.on_activate();
        process_lvgl(5);
    }

    ~ConnectionCallbacksFixture() override {
        if (root_ && lv_obj_is_valid(root_)) {
            lv_obj_delete(root_);
        }
        if (panel_stub_ && lv_obj_is_valid(panel_stub_)) {
            lv_obj_delete(panel_stub_);
        }
        helix::ui::UpdateQueue::instance().drain();
        get_global_settings_panel().deinit_subjects();
        helix::ui::UpdateQueue::instance().drain();

        const std::string key = config_->df();
        config_->set<std::string>(key + "moonraker_host", saved_host_);
        config_->set<int>(key + "moonraker_port", saved_port_);
    }

    bool row_runs(const char* row, const char* callback) {
        lv_obj_t* r = lv_obj_find_by_name(root_, row);
        REQUIRE(r != nullptr);
        lv_event_cb_t cb = lv_xml_get_event_cb(nullptr, callback);
        REQUIRE(cb != nullptr);
        for (uint32_t i = 0; i < lv_obj_get_event_count(r); ++i) {
            if (lv_event_dsc_get_cb(lv_obj_get_event_dsc(r, i)) == cb) {
                return true;
            }
        }
        return false;
    }
};

} // namespace

TEST_CASE_METHOD(ConnectionCallbacksFixture, "Connection page: every row runs its callback",
                 "[settings][connection_callbacks]") {
    CHECK(row_runs("row_network", "on_network_clicked"));
    CHECK(row_runs("row_printers", "on_printers_clicked"));
    CHECK(row_runs("row_printer_host", "on_change_host_clicked"));
}

TEST_CASE_METHOD(ConnectionCallbacksFixture,
                 "Connection page: the Host row shows the configured address",
                 "[settings][connection_callbacks]") {
    lv_obj_t* row = lv_obj_find_by_name(root_, "row_printer_host");
    REQUIRE(row != nullptr);
    lv_obj_t* description = lv_obj_find_by_name(row, "status");
    REQUIRE(description != nullptr);

    CHECK(std::string(lv_label_get_text(description)) == "10.0.0.7:7125");
}
