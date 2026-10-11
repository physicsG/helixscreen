// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#if HELIX_HAS_PLUGINS

#include "../test_fixtures.h"
#include "../test_helpers/plugin_host_test_support.h"
#include "locale_formats.h"
#include "plugin_host.h"
#include "panel_widget_registry.h"
#include "plugin_xml_policy.h"
#include "translation_loader.h"
#include "ui_nav_manager.h"

#include <fstream>
#include <iterator>
#include <lvgl.h>

#include "../catch_amalgamated.hpp"

using Catch::Approx;
using namespace helix::plugin;
using namespace helix::plugin::test;

namespace {

json cb_block(const json& settings) {
    json block = enabled("calibration-bridge", {"http", "moonraker_write"});
    // auto_open off by default in tests: most cases assert the state machine,
    // not the overlay push.
    json s{{"auto_open", false}};
    s.merge_patch(settings);
    block["settings"]["calibration-bridge"] = s;
    return block;
}

/// One available event, shaped as the companion posts it.
json available_event(const char* wizard = "temp_tower") {
    return json{{"wizard", wizard},
                {"filament", "Generic PLA - Voron Rapido"},
                {"printer", "Voron 2.4 300"},
                {"nozzle_mm", 0.4},
                {"current", 220},
                {"choices", json::array({190, 195, 200, 205, 210, 215, 220, 225, 230})},
                {"file", "gcodes/cal_temp_tower.gcode"}};
}

/// Delivers one agent event through every captured notify_agent_event
/// subscription; the binding filters by event name.
void fire_agent_event(HostRig& rig, const char* event, const json& data) {
    for (auto& [name, handler] : rig.fake.notify)
        if (name == "notify_agent_event")
            handler(json{{"params", json::array({json{{"event", event}, {"agent", "orca"}, {"data", data}}})}});
    drain();
}

void set_print_state(const char* v) {
    lv_subject_copy_string(lv_xml_get_subject(nullptr, "print_state"), v);
    drain();
}

std::string cb_str(const char* name) {
    return lv_subject_get_string(lv_xml_get_subject(nullptr, name));
}

int cb_int(const char* name) {
    return lv_subject_get_int(lv_xml_get_subject(nullptr, name));
}

/// Answers every http request since `next` with `status`.
void answer_http(HostRig& rig, size_t& next, int status) {
    while (next < rig.fake.requests.size()) {
        auto& r = rig.fake.requests[next++];
        if (r.kind == "http")
            r.reply(RpcResult{true, json{{"status", status}, {"body", ""}}, {}});
    }
    drain();
}

size_t http_count(HostRig& rig) {
    size_t n = 0;
    for (const auto& r : rig.fake.requests)
        if (r.kind == "http")
            ++n;
    return n;
}

/// Panels seeded the way the app does, so a pushed plugin overlay resolves
/// its base panel (an unregistered push aborts under HELIX_STRICT_OVERLAY_CHECK).
class CalibrationFx : public XMLTestFixture {
  public:
    std::array<lv_obj_t*, UI_PANEL_COUNT> panels{};

    CalibrationFx() {
        for (auto& p : panels)
            p = lv_obj_create(lv_screen_active());
        NavigationManager::instance().set_panels(panels.data());
    }
    ~CalibrationFx() override {
        NavigationManager::instance().go_back();
        drain();
        for (auto& p : panels)
            lv_obj_delete(p);
        drain();
    }
};

/// Available, started, and printed to completion: the asking state.
void run_to_asking(HostRig& rig) {
    fire_agent_event(rig, "helixcal.available", available_event());
    rig.host->dispatch_event("calibration-bridge__start");
    rig.fake.requests[0].reply(RpcResult{true, json{{"ok", true}}, {}});
    drain();
    set_print_state("printing");
    set_print_state("complete");
    REQUIRE(cb_int("calibration-bridge__state") == 3);
}

} // namespace

TEST_CASE_METHOD(XMLTestFixture, "calibration-bridge loads as an overlay-only plugin",
                 "[plugin][example]") {
    HostRig rig(cb_block(json::object()));
    rig.host->load_from("examples/plugins");
    REQUIRE(rig.info("calibration-bridge"));
    CHECK(rig.info("calibration-bridge")->status == PluginStatus::Loaded);
    // No widget: the overlay is the whole UI, so nothing lands in the registry.
    CHECK(helix::find_widget_def("calibration-bridge__tile") == nullptr);

    const std::string path = "examples/plugins/calibration-bridge/ui/calibration-bridge__ask.xml";
    std::ifstream in(path);
    REQUIRE(in);
    std::string xml((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    CHECK(check_plugin_xml("calibration-bridge",
                           {"calibration-bridge__tile", "calibration-bridge__ask"},
                           xml) == std::string());
    CHECK(cb_int("calibration-bridge__state") == 0);
}

TEST_CASE_METHOD(XMLTestFixture, "an available event fills the context and arms Start",
                 "[plugin][example]") {
    HostRig rig(cb_block(json::object()));
    rig.host->load_from("examples/plugins");
    fire_agent_event(rig, "helixcal.available", available_event());
    CHECK(cb_int("calibration-bridge__state") == 1);
    CHECK(cb_str("calibration-bridge__state_title") == "Temperature tower");
    CHECK(cb_str("calibration-bridge__ctx_printer") == "Voron 2.4 300 · nozzle 0.4");
    CHECK(cb_str("calibration-bridge__ctx_filament") == "Generic PLA - Voron Rapido");
    CHECK(cb_str("calibration-bridge__ctx_current") == "Profile: 220°C");
    CHECK(cb_int("calibration-bridge__start_on") == 1);
    CHECK(http_count(rig) == 0);

    // Start reaches Moonraker with the event's file; a 2xx reply arms the
    // printing state.
    rig.host->dispatch_event("calibration-bridge__start");
    REQUIRE(rig.fake.requests.size() == 1);
    CHECK(rig.fake.requests[0].a == "printer.print.start");
    CHECK(rig.fake.requests[0].params == json{{"filename", "gcodes/cal_temp_tower.gcode"}});
    CHECK(cb_int("calibration-bridge__state") == 1); // still available until the reply lands
    rig.fake.requests[0].reply(RpcResult{true, json{{"ok", true}}, {}});
    drain();
    CHECK(cb_int("calibration-bridge__state") == 2);
}

TEST_CASE_METHOD(XMLTestFixture, "print completion hands over to the picker",
                 "[plugin][example]") {
    HostRig rig(cb_block(json::object()));
    rig.host->load_from("examples/plugins");
    fire_agent_event(rig, "helixcal.available", available_event());
    rig.host->dispatch_event("calibration-bridge__start");
    rig.fake.requests[0].reply(RpcResult{true, json{{"ok", true}}, {}});
    drain();
    REQUIRE(cb_int("calibration-bridge__state") == 2);

    set_print_state("printing");
    set_print_state("complete");
    CHECK(cb_int("calibration-bridge__state") == 3);
    CHECK(cb_int("calibration-bridge__pick_on") == 1);
    CHECK(cb_int("calibration-bridge__result_on") == 0);
}

TEST_CASE_METHOD(XMLTestFixture, "a cancelled calibration print returns to available",
                 "[plugin][example]") {
    HostRig rig(cb_block(json::object()));
    rig.host->load_from("examples/plugins");
    fire_agent_event(rig, "helixcal.available", available_event());
    rig.host->dispatch_event("calibration-bridge__start");
    rig.fake.requests[0].reply(RpcResult{true, json{{"ok", true}}, {}});
    drain();

    set_print_state("printing");
    set_print_state("cancelled");
    CHECK(cb_int("calibration-bridge__state") == 1);
    CHECK(cb_str("calibration-bridge__status_line") == "print cancelled");
    CHECK(cb_int("calibration-bridge__start_on") == 1);
}

TEST_CASE_METHOD(XMLTestFixture, "accept posts the chosen value back to the companion",
                 "[plugin][example]") {
    HostRig rig(cb_block(json{{"companion_url", "https://orca-pc:8732/helixcal"}}));
    rig.host->load_from("examples/plugins");
    run_to_asking(rig);
    // The dropdown's bind_value writes LVGL's 0-based selection: 3 is the
    // fourth option, 205 degrees.
    lv_subject_set_int(lv_xml_get_subject(nullptr, "calibration-bridge__choice_idx"), 3);
    rig.host->dispatch_event("calibration-bridge__accept");
    drain();

    REQUIRE(http_count(rig) == 1);
    const FakeBackend::Request* post = nullptr;
    for (const auto& q : rig.fake.requests)
        if (q.kind == "http")
            post = &q;
    REQUIRE(post);
    CHECK(post->b == "https://orca-pc:8732/helixcal");
    CHECK(post->params.value("Content-Type", "") == "application/json");
    const json body = json::parse(post->c);
    CHECK(body.value("event", "") == "chosen");
    CHECK(body.value("wizard", "") == "temp_tower");
    CHECK(body.value("value", 0) == 205);
    CHECK(body.value("current", 0) == 220);
    CHECK(cb_int("calibration-bridge__state") == 4);
    CHECK(cb_str("calibration-bridge__result_value") == "205°C");

    size_t next = 0;
    answer_http(rig, next, 200);
    CHECK(cb_str("calibration-bridge__result_sub") == "sent to the companion");
}

TEST_CASE_METHOD(XMLTestFixture, "without a companion URL the result stays local",
                 "[plugin][example]") {
    HostRig rig(cb_block(json::object()));
    rig.host->load_from("examples/plugins");
    run_to_asking(rig);
    lv_subject_set_int(lv_xml_get_subject(nullptr, "calibration-bridge__choice_idx"), 3);
    rig.host->dispatch_event("calibration-bridge__accept");
    drain();
    CHECK(http_count(rig) == 0);
    CHECK(cb_int("calibration-bridge__state") == 4);
    CHECK(cb_str("calibration-bridge__result_sub") == "(no companion URL set)");
}

TEST_CASE_METHOD(XMLTestFixture, "a cancel event resets to idle", "[plugin][example]") {
    HostRig rig(cb_block(json::object()));
    rig.host->load_from("examples/plugins");
    fire_agent_event(rig, "helixcal.available", available_event());
    REQUIRE(cb_int("calibration-bridge__state") == 1);
    fire_agent_event(rig, "helixcal.cancel", json::object());
    CHECK(cb_int("calibration-bridge__state") == 0);
    CHECK(cb_str("calibration-bridge__state_title") == "No calibration running");
    CHECK(cb_str("calibration-bridge__ctx_filament") == "");
}

TEST_CASE_METHOD(XMLTestFixture, "a malformed available event is a status line",
                 "[plugin][example]") {
    HostRig rig(cb_block(json::object()));
    rig.host->load_from("examples/plugins");
    fire_agent_event(rig, "helixcal.available", json{{"wizard", "temp_tower"}});
    CHECK(cb_int("calibration-bridge__state") == 0);
    CHECK(cb_str("calibration-bridge__status_line") == "bad event");
}

TEST_CASE_METHOD(XMLTestFixture, "a pa pattern formats and posts two decimals",
                 "[plugin][example]") {
    json ev = available_event("pa_pattern");
    ev["choices"] = json::array({0.4, 0.45, 0.5, 0.55});
    ev["current"] = 0.6;
    HostRig rig(cb_block(json{{"companion_url", "https://orca-pc:8732/helixcal"}}));
    rig.host->load_from("examples/plugins");
    fire_agent_event(rig, "helixcal.available", ev);
    CHECK(cb_str("calibration-bridge__state_title") == "PA pattern");
    CHECK(cb_str("calibration-bridge__ctx_current") == "Profile: 0.60");
    rig.host->dispatch_event("calibration-bridge__start");
    rig.fake.requests[0].reply(RpcResult{true, json{{"ok", true}}, {}});
    drain();
    set_print_state("printing");
    set_print_state("complete");
    lv_subject_set_int(lv_xml_get_subject(nullptr, "calibration-bridge__choice_idx"), 1);
    rig.host->dispatch_event("calibration-bridge__accept");
    drain();
    CHECK(cb_str("calibration-bridge__result_value") == "0.45");
    const FakeBackend::Request* post = nullptr;
    for (const auto& q : rig.fake.requests)
        if (q.kind == "http")
            post = &q;
    REQUIRE(post);
    CHECK(json::parse(post->c).value("value", 0.0) == Approx(0.45));
}


TEST_CASE_METHOD(XMLTestFixture, "a pause is not the end of the calibration print",
                 "[plugin][example]") {
    HostRig rig(cb_block(json::object()));
    rig.host->load_from("examples/plugins");
    fire_agent_event(rig, "helixcal.available", available_event());
    rig.host->dispatch_event("calibration-bridge__start");
    rig.fake.requests[0].reply(RpcResult{true, json{{"ok", true}}, {}});
    drain();

    set_print_state("printing");
    set_print_state("paused");
    CHECK(cb_int("calibration-bridge__state") == 2); // still printing, no failure
    CHECK(cb_str("calibration-bridge__status_line") == "");
    set_print_state("printing"); // resumed
    set_print_state("complete");
    CHECK(cb_int("calibration-bridge__state") == 3);
}

TEST_CASE_METHOD(XMLTestFixture, "a double tap of Start issues one print start",
                 "[plugin][example]") {
    HostRig rig(cb_block(json::object()));
    rig.host->load_from("examples/plugins");
    fire_agent_event(rig, "helixcal.available", available_event());
    rig.host->dispatch_event("calibration-bridge__start");
    rig.host->dispatch_event("calibration-bridge__start"); // before the reply
    size_t starts = 0;
    for (const auto& r : rig.fake.requests)
        if (r.a == "printer.print.start")
            ++starts;
    CHECK(starts == 1);
    rig.fake.requests[0].reply(RpcResult{true, json{{"ok", true}}, {}});
    drain();
    CHECK(cb_int("calibration-bridge__state") == 2);
}

TEST_CASE_METHOD(XMLTestFixture, "a failed POST is a note, not a fault",
                 "[plugin][example]") {
    HostRig rig(cb_block(json{{"companion_url", "https://orca-pc:8732/helixcal"}}));
    rig.host->load_from("examples/plugins");
    run_to_asking(rig);
    lv_subject_set_int(lv_xml_get_subject(nullptr, "calibration-bridge__choice_idx"), 3);
    rig.host->dispatch_event("calibration-bridge__accept");
    drain();
    size_t next = 0;
    answer_http(rig, next, 500);
    CHECK(cb_str("calibration-bridge__result_sub") == "HTTP 500");
    CHECK(rig.info("calibration-bridge")->status == PluginStatus::Loaded);
}

TEST_CASE_METHOD(XMLTestFixture, "accept without a selection or a print is a no-op",
                 "[plugin][example]") {
    HostRig rig(cb_block(json{{"companion_url", "https://orca-pc:8732/helixcal"}}));
    rig.host->load_from("examples/plugins");
    fire_agent_event(rig, "helixcal.available", available_event());
    // The state is available, not asking: accept has nothing to answer.
    lv_subject_set_int(lv_xml_get_subject(nullptr, "calibration-bridge__choice_idx"), 0);
    rig.host->dispatch_event("calibration-bridge__accept");
    drain();
    rig.host->dispatch_event("calibration-bridge__accept"); // still no-op
    drain();
    CHECK(http_count(rig) == 0);
    CHECK(cb_int("calibration-bridge__state") == 1);

    // Out-of-range selection (a choice list that shrank): still nothing posted.
    run_to_asking(rig);
    lv_subject_set_int(lv_xml_get_subject(nullptr, "calibration-bridge__choice_idx"), 99);
    rig.host->dispatch_event("calibration-bridge__accept");
    drain();
    CHECK(http_count(rig) == 0);
    CHECK(cb_int("calibration-bridge__state") == 3);
}

TEST_CASE_METHOD(CalibrationFx, "the overlay dropdown carries the choices and the pick",
                 "[plugin][example]") {
    HostRig rig(cb_block(json{{"auto_open", true},
                              {"companion_url", "https://orca-pc:8732/helixcal"}}));
    REQUIRE(register_component("header_bar"));
    rig.host->load_from("examples/plugins");
    fire_agent_event(rig, "helixcal.available", available_event());
    drain();
    CHECK(NavigationManager::instance().has_open_overlays());

    lv_obj_t* dd = lv_obj_find_by_name(lv_screen_active(), "calibration-bridge__pick");
    REQUIRE(dd);
    CHECK(std::string(lv_dropdown_get_options(dd)).rfind("190\u00B0C\n195\u00B0C", 0) == 0);
    CHECK(lv_dropdown_get_selected(dd) == 0); // first option pre-selected

    // The real widget path: selecting drives bind_value onto the subject, and
    // accept posts the value behind the label the user read.
    rig.host->dispatch_event("calibration-bridge__start");
    rig.fake.requests[0].reply(RpcResult{true, json{{"ok", true}}, {}});
    drain();
    set_print_state("printing");
    set_print_state("complete");
    REQUIRE(cb_int("calibration-bridge__state") == 3);
    lv_dropdown_set_selected(dd, 3); // 0-based: the fourth option, 205
    // A tap on the list selects and then sends VALUE_CHANGED; the library
    // call alone does not, and it is the event bind_value listens on.
    lv_obj_send_event(dd, LV_EVENT_VALUE_CHANGED, nullptr);
    drain();
    REQUIRE(lv_subject_get_int(lv_xml_get_subject(nullptr, "calibration-bridge__choice_idx")) == 3);
    rig.host->dispatch_event("calibration-bridge__accept");
    drain();
    REQUIRE(http_count(rig) == 1); // requests[0] is the print start
    for (const auto& q : rig.fake.requests)
        if (q.kind == "http")
            CHECK(json::parse(q.c).value("value", 0) == 205);
}

TEST_CASE_METHOD(XMLTestFixture, "the state machine speaks German", "[plugin][example]") {
    HostRig rig(cb_block(json::object()));
    rig.host->load_from("examples/plugins");
    CHECK(cb_str("calibration-bridge__state_title") == "No calibration running");

    helix::ui::ensure_translation_loaded("de");
    lv_translation_set_language("de");
    helix::ui::locale_set_language("de");
    CHECK(cb_str("calibration-bridge__state_title") == "Keine Kalibrierung aktiv");
    fire_agent_event(rig, "helixcal.available", json{{"wizard", "temp_tower"}});
    CHECK(cb_str("calibration-bridge__status_line") == "fehlerhaftes Ereignis");

    lv_translation_set_language("en");
    helix::ui::locale_set_language("en");
}

#endif // HELIX_HAS_PLUGINS
