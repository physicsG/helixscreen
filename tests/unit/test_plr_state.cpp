// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_plr_state.cpp
 * @brief Unit tests for PrinterState::pl_env_valid / pl_recovery_file
 *
 * Snapmaker-fork Power-Loss-Recovery fields carried on virtual_sdcard.
 * Absent (harmless) on mainline Klipper; Moonraker sends explicit null for
 * subscribed fields the connected firmware doesn't populate, so parsing must
 * type-check every field rather than assume presence implies a usable value.
 */

#include "../test_helpers/printer_state_test_access.h"
#include "../ui_test_utils.h"
#include "printer_print_state.h"
#include "printer_state.h"

#include <spdlog/spdlog.h>

#include "../catch_amalgamated.hpp"

using namespace helix;
using json = nlohmann::json;

namespace {

class PlrStateTestFixture {
  public:
    PlrStateTestFixture() {
        lv_init_safe();

        if (!display_created_) {
            display_ = lv_display_create(480, 320);
            alignas(64) static lv_color_t buf[480 * 10];
            lv_display_set_buffers(display_, buf, nullptr, sizeof(buf),
                                   LV_DISPLAY_RENDER_MODE_PARTIAL);
            lv_display_set_flush_cb(display_, [](lv_display_t* disp, const lv_area_t*, uint8_t*) {
                lv_display_flush_ready(disp);
            });
            display_created_ = true;
        }

        PrinterStateTestAccess::reset(state_);
        state_.init_subjects(false);
    }

    ~PlrStateTestFixture() {
        PrinterStateTestAccess::reset(state_);
    }

  protected:
    PrinterState& state() {
        return state_;
    }

    bool pl_env_valid() {
        return state_.is_pl_env_valid();
    }

    const std::string& pl_recovery_file() {
        return state_.pl_recovery_file();
    }

    bool creality_plr_capable() {
        return state_.is_creality_plr_capable();
    }

  private:
    PrinterState state_;
    static lv_display_t* display_;
    static bool display_created_;
};

lv_display_t* PlrStateTestFixture::display_ = nullptr;
bool PlrStateTestFixture::display_created_ = false;

} // namespace

TEST_CASE_METHOD(PlrStateTestFixture, "PLR state: default is invalid with no recovery file",
                 "[plr][state]") {
    REQUIRE(pl_env_valid() == false);
    REQUIRE(pl_recovery_file().empty());
}

TEST_CASE_METHOD(PlrStateTestFixture, "PLR state: pl_env_valid=true + file_path parses",
                 "[plr][state]") {
    json status = {{"virtual_sdcard",
                    {{"progress", 0.0},
                     {"layer", nullptr},
                     {"layer_count", nullptr},
                     {"is_active", false},
                     {"pl_env_valid", true},
                     {"file_path", "benchy.gcode"}}}};
    state().update_from_status(status);
    REQUIRE(pl_env_valid() == true);
    REQUIRE(pl_recovery_file() == "benchy.gcode");
}

TEST_CASE_METHOD(PlrStateTestFixture, "PLR state: explicit null pl_env_valid leaves default",
                 "[plr][state]") {
    // Mainline Klipper (no power_loss_check module) subscribes the field but
    // Moonraker sends an explicit null since virtual_sdcard never sets it.
    json status = {{"virtual_sdcard",
                    {{"progress", 0.0},
                     {"is_active", false},
                     {"pl_env_valid", nullptr},
                     {"file_path", nullptr}}}};
    state().update_from_status(status);
    REQUIRE(pl_env_valid() == false);
    REQUIRE(pl_recovery_file().empty());
}

TEST_CASE_METHOD(PlrStateTestFixture, "PLR state: missing fields entirely leaves default",
                 "[plr][state]") {
    json status = {{"virtual_sdcard", {{"progress", 0.0}, {"is_active", false}}}};
    state().update_from_status(status);
    REQUIRE(pl_env_valid() == false);
    REQUIRE(pl_recovery_file().empty());
}

TEST_CASE_METHOD(PlrStateTestFixture, "PLR state: file_path change is applied (changed-guard)",
                 "[plr][state]") {
    // update_from_status only assigns pl_recovery_file_ when the incoming
    // file_path differs (avoids a per-status temporary). A genuine change must
    // still flow through.
    json first = {{"virtual_sdcard",
                   {{"progress", 0.0},
                    {"is_active", false},
                    {"pl_env_valid", true},
                    {"file_path", "first.gcode"}}}};
    state().update_from_status(first);
    REQUIRE(pl_recovery_file() == "first.gcode");

    json second = {{"virtual_sdcard",
                    {{"progress", 0.0},
                     {"is_active", false},
                     {"pl_env_valid", true},
                     {"file_path", "second.gcode"}}}};
    state().update_from_status(second);
    REQUIRE(pl_recovery_file() == "second.gcode");
}

TEST_CASE_METHOD(PlrStateTestFixture,
                 "PLR state: disconnect-edge reset clears pl_env_valid + recovery file",
                 "[plr][state]") {
    json valid = {{"virtual_sdcard",
                   {{"progress", 0.0},
                    {"is_active", false},
                    {"pl_env_valid", true},
                    {"file_path", "interrupted.gcode"}}}};
    state().update_from_status(valid);
    REQUIRE(pl_env_valid() == true);
    REQUIRE(pl_recovery_file() == "interrupted.gcode");

    // The offer controller performs exactly this on a CONNECTED->not-CONNECTED
    // edge so a reconnect re-derives a genuine 0->1 pl_env_valid edge from the
    // fresh status: force the subject to 0 and drop the stale recovery file.
    lv_subject_set_int(state().get_pl_env_valid_subject(), 0);
    state().clear_pl_recovery_file();
    REQUIRE(pl_env_valid() == false);
    REQUIRE(pl_recovery_file().empty());
}

TEST_CASE_METHOD(PlrStateTestFixture, "PLR state: pl_env_valid flips true then back to false",
                 "[plr][state]") {
    json valid = {{"virtual_sdcard",
                   {{"progress", 0.0},
                    {"is_active", false},
                    {"pl_env_valid", true},
                    {"file_path", "recovered.gcode"}}}};
    state().update_from_status(valid);
    REQUIRE(pl_env_valid() == true);

    // Boot-time recovery consumed (e.g. SDCARD_PRINT_PL_CLEAR_ENV or a fresh
    // print start clears the env) — firmware reports false on the next update.
    json cleared = {
        {"virtual_sdcard", {{"progress", 0.0}, {"is_active", false}, {"pl_env_valid", false}}}};
    state().update_from_status(cleared);
    REQUIRE(pl_env_valid() == false);
}

// ===========================================================================
// Creality backend capability — print_stats.power_loss
//
// The key exists ONLY in Creality's Klipper fork; mainline has no such field.
// PRESENCE (as a JSON number) is the capability marker, not the value — it
// normally reads 0 and only becomes 1 after the side-effectful detect probe.
// ===========================================================================

TEST_CASE_METHOD(PlrStateTestFixture, "PLR capability: default is not Creality-capable",
                 "[plr][state][creality]") {
    REQUIRE(creality_plr_capable() == false);
}

TEST_CASE_METHOD(PlrStateTestFixture,
                 "PLR capability: print_stats.power_loss==0 still marks Creality capable",
                 "[plr][state][creality]") {
    // This is the normal idle value on a K1C/K2. Gating on the VALUE would make
    // the backend undetectable until after a probe we would never fire.
    json status = {{"print_stats", {{"state", "standby"}, {"power_loss", 0}}}};
    state().update_from_status(status);
    REQUIRE(creality_plr_capable() == true);
}

TEST_CASE_METHOD(PlrStateTestFixture, "PLR capability: explicit null power_loss is NOT capability",
                 "[plr][state][creality]") {
    // Moonraker answers a subscribed-but-unpopulated field with an explicit
    // null. Treating that as presence would fire the side-effectful Creality
    // probe at every mainline-Klipper printer.
    json status = {{"print_stats", {{"state", "standby"}, {"power_loss", nullptr}}}};
    state().update_from_status(status);
    REQUIRE(creality_plr_capable() == false);
}

TEST_CASE_METHOD(PlrStateTestFixture, "PLR capability: absent power_loss key is NOT capability",
                 "[plr][state][creality]") {
    json status = {{"print_stats", {{"state", "standby"}}}};
    state().update_from_status(status);
    REQUIRE(creality_plr_capable() == false);
}

TEST_CASE_METHOD(PlrStateTestFixture, "PLR capability: latches up, survives a status delta",
                 "[plr][state][creality]") {
    // Moonraker sends DELTAS: a later print_stats notification carrying only
    // print_duration has no power_loss key. Capability must not flicker off, or
    // the offer controller would see a spurious 1->0->1 edge and re-probe.
    json first = {{"print_stats", {{"state", "standby"}, {"power_loss", 0}}}};
    state().update_from_status(first);
    REQUIRE(creality_plr_capable() == true);

    json delta = {{"print_stats", {{"print_duration", 12.0}}}};
    state().update_from_status(delta);
    REQUIRE(creality_plr_capable() == true);
}

TEST_CASE_METHOD(PlrStateTestFixture, "PLR capability: power_loss==1 is also capability",
                 "[plr][state][creality]") {
    json status = {{"print_stats", {{"state", "standby"}, {"power_loss", 1}}}};
    state().update_from_status(status);
    REQUIRE(creality_plr_capable() == true);
}
