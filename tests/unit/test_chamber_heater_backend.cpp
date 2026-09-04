// tests/unit/test_chamber_heater_backend.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include "chamber_heater_backend.h"

#include "../catch_amalgamated.hpp"

using namespace helix::chamber;

TEST_CASE("generic backend keeps keyword tiers", "[chamber][backend]") {
    const auto* generic = backend_by_id("generic");
    REQUIRE(generic != nullptr);
    // Tiers preserved from printer_discovery.h chamber_keyword_confidence
    CHECK(generic->discovery_confidence("chamber") == 100);
    CHECK(generic->discovery_confidence("chamber_heater") == 99); // compound penalty
    CHECK(generic->discovery_confidence("enclosure") == 90);
    CHECK(generic->discovery_confidence("cavity") == 85);
    CHECK(generic->discovery_confidence("box") == 60);
    CHECK(generic->discovery_confidence("heater_box1") ==
          0); // "BOX1" not standalone BOX (AFC dryer)
    CHECK(generic->discovery_confidence("hotend") == 0);
    CHECK(generic->discovery_confidence("chamber_humidity") ==
          59); // 100 -1 compound -40 air-quality
    // Original tokenizer splits ONLY on _/whitespace — hyphen is not a separator.
    CHECK(generic->discovery_confidence("chamber-tvoc") == 99); // no air-quality penalty
    CHECK(generic->discovery_confidence("my-box") == 0);        // BOX not standalone
}

TEST_CASE("registry exposes generic as default", "[chamber][backend]") {
    CHECK(registry().empty() == false);
    CHECK(backend_by_id("generic") == registry().front());
}

TEST_CASE("match dispatches to best backend", "[chamber][backend]") {
    CHECK(match("heater_generic chamber").backend == backend_by_id("generic"));
    CHECK(match("heater_generic chamber").confidence > 0);
    CHECK(match("hotend").backend == nullptr); // nothing claims it
}

TEST_CASE("dragonbreath backend matches names and ceiling", "[chamber][backend]") {
    const auto* db = backend_by_id("dragonbreath");
    REQUIRE(db != nullptr);
    CHECK(db->discovery_confidence("dragonbreath") == 95);
    CHECK(db->discovery_confidence("heater_generic dragonbreath") == 95);
    CHECK(db->discovery_confidence("heater_generic chamber") == 0);
    CHECK(db->diagnostics_object() == "dragonbreath");
    CHECK(db->filter_fan_pin() == "output_pin dragonbreath_filter");
    CHECK(db->fault_reset_gcode() == "DRAGONBREATH_RESET");
    CHECK(db->conservative_max_temp() == 60.0);
    CHECK(db->device_autonomous_control() == false);
}

TEST_CASE("dragonbreath parse: live nominal payload", "[chamber][backend]") {
    const auto* db = backend_by_id("dragonbreath");
    // Paste the Reference-payload nominal JSON at the top of this plan:
    nlohmann::json j = nlohmann::json::parse(R"({"temperature":25.5,"target":0.0,
      "connected":true,"heating":false,"fault":false,"inhibited":false,
      "fault_reason":null,"ptc_temp":24.9,"fan_percent":0,"fan_reason":"off",
      "mode":"off","source":"klipper","lease_owned":false})");
    auto d = db->parse_diagnostics(j);
    REQUIRE(d.has_value());
    CHECK(d->fault == false);
    CHECK(d->inhibited == false);
    CHECK(d->fault_reason.empty());
    CHECK(d->element_temp_c == Catch::Approx(24.9));
    CHECK(d->filter_fan_percent == 0);
    CHECK(d->filter_fan_reason == "off");
    CHECK(d->externally_controlled == false);
}

TEST_CASE("dragonbreath parse: faulted + external-control variants", "[chamber][backend]") {
    const auto* db = backend_by_id("dragonbreath");
    auto faulted = db->parse_diagnostics(nlohmann::json::parse(R"({"fault":true,
      "inhibited":false,"fault_reason":"ptc_overtemp","ptc_temp":106.2,
      "fan_percent":100,"fan_reason":"purge","mode":"off","source":"device",
      "lease_owned":false,"connected":true})"));
    REQUIRE(faulted.has_value());
    CHECK(faulted->fault == true);
    CHECK(faulted->fault_reason == "ptc_overtemp");
    CHECK(faulted->fault_reason_kind == FaultReason::Overtemp);
    CHECK(faulted->element_temp_c == Catch::Approx(106.2));

    auto ext = db->parse_diagnostics(nlohmann::json::parse(R"({"fault":false,
      "inhibited":false,"fault_reason":null,"ptc_temp":30.1,"fan_percent":40,
      "fan_reason":"filter","mode":"power_on","source":"webui",
      "lease_owned":false,"connected":true})"));
    REQUIRE(ext.has_value());
    CHECK(ext->externally_controlled == true); // heating, source != klipper, no lease
}

TEST_CASE("dragonbreath fault codes classify to generic kinds", "[chamber][backend]") {
    const auto* db = backend_by_id("dragonbreath");
    REQUIRE(db != nullptr);

    // Substring-heuristic table: the live "ptc_overtemp" plus the documented
    // families (overheat / sensor-short-open / comms-timeout-watchdog-
    // disconnect). Anything unrecognized is Other, never a vendor leak.
    struct Case {
        const char* code;
        FaultReason expected;
    };
    const Case cases[] = {
        {"ptc_overtemp", FaultReason::Overtemp},
        {"element_overheat", FaultReason::Overtemp},
        {"ptc_sensor_fault", FaultReason::SensorFault},
        {"ptc_short_circuit", FaultReason::SensorFault},
        {"thermistor_open", FaultReason::SensorFault},
        {"comms_timeout", FaultReason::CommsLoss},
        {"host_watchdog", FaultReason::CommsLoss},
        {"link_disconnect", FaultReason::CommsLoss},
        {"mystery_code", FaultReason::Other},
    };
    for (const auto& c : cases) {
        CAPTURE(c.code);
        auto d = db->parse_diagnostics(
            nlohmann::json{{"fault", true}, {"fault_reason", c.code}, {"ptc_temp", 24.9}});
        REQUIRE(d.has_value());
        CHECK(d->fault_reason == c.code); // raw code preserved for logs
        CHECK(d->fault_reason_kind == c.expected);
    }

    // No reason at all -> None.
    auto nominal = db->parse_diagnostics(nlohmann::json{{"ptc_temp", 24.9}});
    REQUIRE(nominal.has_value());
    CHECK(nominal->fault_reason_kind == FaultReason::None);
}

TEST_CASE("dragonbreath parse tolerates null lease fields", "[chamber][backend]") {
    const auto* db = backend_by_id("dragonbreath");
    // .value() throws type_error.302 when a key is present but null; the
    // schema really emits nulls (mock synthesizes fault_reason: null).
    auto d = db->parse_diagnostics(nlohmann::json::parse(R"({"fault":false,
      "inhibited":false,"fault_reason":null,"ptc_temp":24.9,"fan_percent":0,
      "fan_reason":"off","mode":null,"source":null,"lease_owned":null})"));
    REQUIRE(d.has_value());
    CHECK(d->externally_controlled == false);
}

TEST_CASE("dragonbreath parse rejects foreign payloads", "[chamber][backend]") {
    const auto* db = backend_by_id("dragonbreath");
    CHECK_FALSE(
        db->parse_diagnostics(nlohmann::json::parse(R"({"temperature":21.0})")).has_value());
    CHECK_FALSE(db->parse_diagnostics(nlohmann::json::parse("7")).has_value());
}

TEST_CASE("panda_breath backend: heater + ceiling only", "[chamber][backend]") {
    const auto* pb = backend_by_id("panda_breath");
    REQUIRE(pb != nullptr);
    CHECK(pb->discovery_confidence("heater_generic panda_breath") == 95);
    CHECK(pb->discovery_confidence("heater_generic pandabreath") == 95);
    CHECK(pb->discovery_confidence("heater_generic chamber") == 0);
    CHECK(pb->diagnostics_object().empty()); // stock schema unverified — no surface
    CHECK(pb->filter_fan_pin().empty());
    CHECK(pb->conservative_max_temp() == 60.0);
    CHECK(pb->device_autonomous_control() == true); // stock Auto drives from bed temp
    CHECK_FALSE(pb->parse_diagnostics(nlohmann::json::object()).has_value());
}

TEST_CASE("appliance beats generic on its own name", "[chamber][backend]") {
    CHECK(match("heater_generic dragonbreath").backend == backend_by_id("dragonbreath"));
    CHECK(match("heater_generic panda_breath").backend == backend_by_id("panda_breath"));
    // printer-native chamber still wins over appliance tiers (100 > 95)
    CHECK(match("heater_generic chamber").backend == backend_by_id("generic"));
}
