// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_print_start_analysis_refresh.cpp
 * @brief The PRINT_START analysis follows the printer's current config.
 *
 * A reconnect (printer switch, Moonraker restart) or a Klipper restart (the
 * usual way an edited macro takes effect) must re-read the macro, and an
 * analysis already in flight when that happens must not be the one kept.
 * The manager's observers and the analysis result both go through the
 * UpdateQueue, one batch per drain, so a case can stop with a result in flight.
 */

#include "ui_panel_print_select.h"
#include "ui_print_preparation_manager.h"
#include "ui_update_queue.h"

#include "../lvgl_ui_test_fixture.h"
#include "../test_helpers/print_select_panel_test_access.h"
#include "helix-xml/src/xml/lv_xml.h"
#include "moonraker_api_mock.h"
#include "moonraker_client_mock.h"
#include "printer_state.h"

#include <memory>
#include <string>

#include "../catch_amalgamated.hpp"

using helix::ConnectionState;
using helix::KlippyState;
using helix::PrintStartOpCategory;
using helix::ui::PrintPreparationManager;
using helix::ui::UpdateQueue;

namespace {

std::string print_start_cfg(const char* body) {
    return std::string("[gcode_macro PRINT_START]\ngcode:\n") + body;
}

const char* MESH_MACRO = "  G28\n  BED_MESH_CALIBRATE\n";
const char* QGL_MACRO = "  G28\n  QUAD_GANTRY_LEVEL\n";

class AnalysisRefreshFixture : public LVGLUITestFixture {
  public:
    MoonrakerClientMock client_;
    MoonrakerAPIMock api_{client_, state()};
    PrintPreparationManager manager_;

    AnalysisRefreshFixture() {
        state().init_subjects(false);
        client_.connect("ws://mock/websocket", []() {}, []() {});
        set_connection(ConnectionState::DISCONNECTED);
        serve_macro(MESH_MACRO);
        manager_.set_dependencies(&api_, &state());
    }

    ~AnalysisRefreshFixture() override {
        settle();
        client_.disconnect();
    }

    void serve_macro(const char* body) {
        api_.set_config_files({{"printer.cfg", print_start_cfg(body)}});
    }

    void set_connection(ConnectionState s) {
        state().network_state().set_printer_connection_state_internal(static_cast<int>(s), "");
    }

    void settle() {
        for (int i = 0; i < 4; ++i) {
            UpdateQueue::instance().drain();
        }
    }

    bool analysis_has(PrintStartOpCategory category) const {
        const auto& analysis = manager_.get_macro_analysis();
        return analysis.has_value() && analysis->has_operation(category);
    }
};

} // namespace

TEST_CASE_METHOD(AnalysisRefreshFixture, "a reconnect re-reads an edited PRINT_START",
                 "[print_preparation][print_start][1233]") {
    set_connection(ConnectionState::CONNECTED);
    settle();
    REQUIRE(manager_.has_macro_analysis());
    REQUIRE(analysis_has(PrintStartOpCategory::BED_MESH));
    REQUIRE_FALSE(analysis_has(PrintStartOpCategory::QGL));

    serve_macro(QGL_MACRO);
    set_connection(ConnectionState::DISCONNECTED);
    set_connection(ConnectionState::CONNECTED);
    settle();

    CHECK(analysis_has(PrintStartOpCategory::QGL));
    CHECK_FALSE(analysis_has(PrintStartOpCategory::BED_MESH));
}

TEST_CASE_METHOD(AnalysisRefreshFixture, "a Klipper restart re-reads an edited PRINT_START",
                 "[print_preparation][print_start][1233]") {
    state().set_klippy_state_sync(KlippyState::READY);
    set_connection(ConnectionState::CONNECTED);
    settle();
    REQUIRE(analysis_has(PrintStartOpCategory::BED_MESH));

    serve_macro(QGL_MACRO);
    state().set_klippy_state_sync(KlippyState::SHUTDOWN);
    state().set_klippy_state_sync(KlippyState::READY);
    settle();

    CHECK(analysis_has(PrintStartOpCategory::QGL));
    CHECK_FALSE(analysis_has(PrintStartOpCategory::BED_MESH));
}

TEST_CASE_METHOD(AnalysisRefreshFixture,
                 "klippy coming ready before the first connect is no restart",
                 "[print_preparation][print_start][1233]") {
    // Boot order: Klippy reads not-ready until its first status, often before connect.
    state().set_klippy_state_sync(KlippyState::SHUTDOWN);
    settle();
    state().set_klippy_state_sync(KlippyState::READY);
    settle();

    CHECK_FALSE(manager_.is_macro_analysis_in_progress());
    CHECK_FALSE(manager_.get_macro_analysis().has_value());
}

TEST_CASE_METHOD(AnalysisRefreshFixture,
                 "a reconnect during an analysis keeps the newer macro, not the one in flight",
                 "[print_preparation][print_start][1233]") {
    set_connection(ConnectionState::CONNECTED);
    // One batch: the observer starts the analysis, its result is still queued.
    UpdateQueue::instance().drain();
    REQUIRE(manager_.is_macro_analysis_in_progress());

    serve_macro(QGL_MACRO);
    set_connection(ConnectionState::DISCONNECTED);
    set_connection(ConnectionState::CONNECTED);
    settle();

    CHECK_FALSE(manager_.is_macro_analysis_in_progress());
    CHECK(analysis_has(PrintStartOpCategory::QGL));
    CHECK_FALSE(analysis_has(PrintStartOpCategory::BED_MESH));
}

TEST_CASE_METHOD(AnalysisRefreshFixture, "re-wiring dependencies reuses the analysis it has",
                 "[print_preparation][print_start][1233]") {
    set_connection(ConnectionState::CONNECTED);
    settle();
    REQUIRE(analysis_has(PrintStartOpCategory::BED_MESH));

    // The detail view re-wires on every widget-tree rebuild; that is not a
    // reconnect and must not refetch.
    serve_macro(QGL_MACRO);
    manager_.set_dependencies(&api_, &state());
    settle();

    CHECK(analysis_has(PrintStartOpCategory::BED_MESH));
    CHECK_FALSE(manager_.is_macro_analysis_in_progress());
}

TEST_CASE_METHOD(AnalysisRefreshFixture, "print select analyzes PRINT_START before any file opens",
                 "[print_preparation][print_start][print_select][1233]") {
    auto panel = std::make_unique<PrintSelectPanel>(state(), &api_);
    panel->init_subjects();
    auto* root =
        static_cast<lv_obj_t*>(lv_xml_create(test_screen(), "print_select_panel", nullptr));
    REQUIRE(root != nullptr);
    panel->setup(root, test_screen());

    set_connection(ConnectionState::CONNECTED);
    settle();

    REQUIRE_FALSE(PrintSelectPanelTestAccess::detail_view_built(*panel));
    auto* prep = PrintSelectPanelTestAccess::prep_manager(*panel);
    REQUIRE(prep != nullptr);
    CHECK(prep->has_macro_analysis());
    CHECK(prep->get_macro_analysis()->has_operation(PrintStartOpCategory::BED_MESH));

    settle();
    lv_obj_delete(root);
    settle();
    panel.reset();
    settle();
}
