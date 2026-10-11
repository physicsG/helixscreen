// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#include "../helix_test_fixture.h"
#include "../test_helpers/config_test_access.h"
#include "app_globals.h"
#include "detection_manager.h"
#include "moonraker_client_mock.h"
#include "settings_manager.h"
#include "u1_stock_detection_source.h"

#include "../catch_amalgamated.hpp"

using namespace helix::detection;
using helix::Config;
using helix::ConfigTestAccess;
using helix::SettingsManager;
using json = nlohmann::json;
namespace {
struct StubSource : DetectionSource {
    std::string id() const override {
        return "stub";
    }
    bool available() const override {
        return avail;
    }
    void set_callback(Callback cb) override {
        saved = std::move(cb);
    }
    void fire(const DetectionEvent& e) {
        if (saved)
            saved(e);
    }
    bool avail = true;
    bool tuneable = false; ///< returned by can_tune()
    int tune_calls = 0;
    bool can_tune() const override {
        return tuneable;
    }
    void tune() override {
        ++tune_calls;
    }
    std::optional<DetectionPreference> pref; ///< returned by printer_preference()
    std::optional<DetectionPreference> printer_preference() const override {
        return pref;
    }
    Callback saved;
};
} // namespace

TEST_CASE_METHOD(HelixTestFixture, "DetectionManager dispatches to presenter under DeferToSource",
                 "[detection][manager]") {
    auto& m = DetectionManager::instance();
    m.reset_for_test();
    auto stub = std::make_unique<StubSource>();
    auto* raw = stub.get();
    m.register_source(std::move(stub));
    m.set_policy("stub", DetectionPolicy::DeferToSource);
    int modal_shown = 0;
    DetectionEvent seen;
    m.set_presenter([&](const DetectionEvent& e, DetectionPolicy p) {
        if (p == DetectionPolicy::DeferToSource) {
            ++modal_shown;
            seen = e;
        }
    });
    DetectionEvent e;
    e.source_id = "stub";
    e.kind = DetectionKind::Spaghetti;
    e.attributable = true;
    e.already_paused = true;
    e.message = "detected noodle";
    raw->fire(e);
    REQUIRE(modal_shown == 1);
    REQUIRE(seen.kind == DetectionKind::Spaghetti);
}

TEST_CASE_METHOD(HelixTestFixture, "DetectionManager respects Off policy", "[detection][manager]") {
    auto& m = DetectionManager::instance();
    m.reset_for_test();
    auto stub = std::make_unique<StubSource>();
    auto* raw = stub.get();
    m.register_source(std::move(stub));
    m.set_policy("stub", DetectionPolicy::Off);
    int calls = 0;
    m.set_presenter([&](const DetectionEvent&, DetectionPolicy) { ++calls; });
    DetectionEvent e;
    e.source_id = "stub";
    e.kind = DetectionKind::Spaghetti;
    raw->fire(e);
    REQUIRE(calls == 0);
}

TEST_CASE_METHOD(HelixTestFixture, "DetectionManager any_available reflects sources",
                 "[detection][manager]") {
    auto& m = DetectionManager::instance();
    m.reset_for_test();
    auto stub = std::make_unique<StubSource>();
    stub->avail = false;
    auto* raw = stub.get();
    m.register_source(std::move(stub));
    REQUIRE_FALSE(m.any_available());
    raw->avail = true;
    REQUIRE(m.any_available());
}

TEST_CASE_METHOD(HelixTestFixture, "DetectionManager source_can_tune asks the source",
                 "[detection][manager]") {
    auto& m = DetectionManager::instance();
    m.reset_for_test();
    // StubSource leaves can_tune() at its default: no Tune button for it, and
    // none for an id that was never registered.
    m.register_source(std::make_unique<StubSource>());
    REQUIRE_FALSE(m.source_can_tune("stub"));
    REQUIRE_FALSE(m.source_can_tune("nobody"));

    auto u1 = std::make_unique<U1StockSource>(nullptr);
    m.register_source(std::move(u1));
    REQUIRE(m.source_can_tune(U1StockSource::SOURCE_ID));
}

TEST_CASE_METHOD(HelixTestFixture, "DetectionManager tune_source dispatches, gated on can_tune",
                 "[detection][manager]") {
    auto& m = DetectionManager::instance();
    m.reset_for_test();

    SECTION("a source that cannot tune is not asked") {
        auto stub = std::make_unique<StubSource>();
        auto* raw = stub.get();
        m.register_source(std::move(stub));
        m.tune_source("stub");
        CHECK(raw->tune_calls == 0);
    }

    SECTION("an unknown id dispatches nothing") {
        auto stub = std::make_unique<StubSource>();
        auto* raw = stub.get();
        stub->tuneable = true;
        m.register_source(std::move(stub));
        m.tune_source("nobody");
        CHECK(raw->tune_calls == 0);
    }

    SECTION("the U1 source sends its own tuning macro") {
        MoonrakerClientMock client(MoonrakerClientMock::PrinterType::VORON_24);
        set_moonraker_client(&client);
        m.register_source(std::make_unique<U1StockSource>(nullptr));
        m.tune_source(U1StockSource::SOURCE_ID);
        set_moonraker_client(nullptr);
        REQUIRE(client.last_send_method() == "printer.gcode.script");
        CHECK(client.last_send_script() == "DEFECT_DETECTION_CONFIG NOODLE_SENSITIVITY=low");
    }
}

TEST_CASE_METHOD(HelixTestFixture, "DetectionManager capability probe sets U1 source available",
                 "[detection][manager]") {
    auto& m = DetectionManager::instance();
    m.reset_for_test();

    // U1 source starts unavailable (capable_ defaults false). No PrinterState needed:
    // apply_objects_list_for_test() only drives the capability flag, not the observer.
    auto u1 = std::make_unique<U1StockSource>(nullptr);
    auto* raw = u1.get();
    m.register_source(std::move(u1));
    REQUIRE_FALSE(raw->available());

    // objects.list WITHOUT defect_detection -> stays unavailable.
    REQUIRE_FALSE(m.apply_objects_list_for_test(json::array({"gcode_move", "toolhead"})));
    REQUIRE_FALSE(raw->available());

    // objects.list WITH defect_detection -> becomes available.
    REQUIRE(
        m.apply_objects_list_for_test(json::array({"gcode_move", "defect_detection", "toolhead"})));
    REQUIRE(raw->available());

    // Probe again without it -> capability is re-cleared (idempotent, reflects state).
    REQUIRE_FALSE(m.apply_objects_list_for_test(json::array({"gcode_move"})));
    REQUIRE_FALSE(raw->available());
}

namespace {
/// SettingsManager subjects persist across tests in this binary while the
/// Config sandbox resets, so rebuild them from Config the way
/// test_k2_stock_detection_source.cpp does and restore known values at the end
/// of each case.
struct DetectionSettingsGuard {
    DetectionSettingsGuard() {
        ConfigTestAccess::data(*Config::get_instance()).erase("detection");
        SettingsManager::instance().deinit_subjects();
        SettingsManager::instance().init_subjects();
    }
    ~DetectionSettingsGuard() {
        SettingsManager::instance().set_detection_enabled(true);
        SettingsManager::instance().set_detection_pause_on_detect(true);
    }
};
} // namespace

TEST_CASE_METHOD(HelixTestFixture, "DetectionManager seeds detection settings once",
                 "[detection][manager]") {
    DetectionSettingsGuard guard;
    auto& m = DetectionManager::instance();
    m.reset_for_test();
    auto& sm = SettingsManager::instance();
    REQUIRE_FALSE(sm.is_detection_seeded());

    SECTION("a capable source's preference is copied into the settings") {
        auto stub = std::make_unique<StubSource>();
        stub->pref = DetectionPreference{false, false};
        m.register_source(std::move(stub));
        CHECK_FALSE(sm.get_detection_enabled());
        CHECK_FALSE(sm.get_detection_pause_on_detect());
        CHECK(sm.is_detection_seeded());
    }

    SECTION("no stored preference keeps the defaults, but the seed still runs") {
        auto stub = std::make_unique<StubSource>();
        m.register_source(std::move(stub));
        CHECK(sm.get_detection_enabled());
        CHECK(sm.get_detection_pause_on_detect());
        CHECK(sm.is_detection_seeded());
    }

    SECTION("once seeded, later starts never re-seed") {
        sm.mark_detection_seeded();
        sm.set_detection_enabled(true);
        auto stub = std::make_unique<StubSource>();
        stub->pref = DetectionPreference{false, false};
        m.register_source(std::move(stub));
        CHECK(sm.get_detection_enabled());
        CHECK(sm.get_detection_pause_on_detect());
    }

    SECTION("no capable source: nothing seeds") {
        auto stub = std::make_unique<StubSource>();
        stub->avail = false;
        stub->pref = DetectionPreference{false, false};
        m.register_source(std::move(stub));
        CHECK_FALSE(sm.is_detection_seeded());
        CHECK(sm.get_detection_enabled());
        CHECK(sm.get_detection_pause_on_detect());
    }
}

TEST_CASE_METHOD(HelixTestFixture, "DetectionManager response_for combines policy and settings",
                 "[detection][manager]") {
    DetectionSettingsGuard guard;
    auto& m = DetectionManager::instance();
    auto& sm = SettingsManager::instance();
    sm.set_detection_enabled(true);
    sm.set_detection_pause_on_detect(true);

    CHECK(m.response_for(DetectionPolicy::DeferToSource) == DetectionResponse::PauseAndRespond);
    CHECK(m.response_for(DetectionPolicy::NotifyOnly) == DetectionResponse::WarnOnly);
    CHECK(m.response_for(DetectionPolicy::Off) == DetectionResponse::Suppressed);

    // Pause-on-detect off means warn only, whichever source reported and
    // whether or not the printer paused itself.
    sm.set_detection_pause_on_detect(false);
    CHECK(m.response_for(DetectionPolicy::DeferToSource) == DetectionResponse::WarnOnly);

    sm.set_detection_enabled(false);
    CHECK(m.response_for(DetectionPolicy::DeferToSource) == DetectionResponse::Suppressed);
    CHECK(m.response_for(DetectionPolicy::NotifyOnly) == DetectionResponse::Suppressed);
}

TEST_CASE_METHOD(HelixTestFixture, "DetectionManager availability subject tracks capability",
                 "[detection][manager]") {
    auto& m = DetectionManager::instance();
    m.reset_for_test();

    auto u1 = std::make_unique<U1StockSource>(nullptr);
    m.register_source(std::move(u1));
    CHECK(lv_subject_get_int(m.subject_detection_available()) == 0);

    // The probe path (defect_detection present) flips both source and subject.
    m.apply_objects_list_for_test(json::array({"defect_detection"}));
    CHECK(lv_subject_get_int(m.subject_detection_available()) == 1);
    // The U1's firmware pauses by itself, so even capable the pause setting
    // does not apply to it.
    CHECK(lv_subject_get_int(m.subject_detection_pause_applicable()) == 0);

    // And back: the subject is re-evaluated on every capability refresh.
    m.apply_objects_list_for_test(json::array({"gcode_move"}));
    CHECK(lv_subject_get_int(m.subject_detection_available()) == 0);
}
