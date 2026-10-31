// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#include "printer_detector.h"
#include "printer_image_variants.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <regex>
#include <set>
#include <string>
#include <vector>

#include "catch_amalgamated.hpp"
#include "hv/json.hpp"

// A heuristic's `reason` is the evidence a future re-tune trusts. When it claims
// an object is specific to one model and captured hardware for another model
// reports the same object, the claim is false and stays false until someone
// re-scores the entry on the strength of it.
//
// Objects a real Creality K1C reports, from the captured object list in
// tests/unit/test_printer_discovery_real_hardware.cpp ("PrinterDiscovery: K1C
// classifies output_pin light vs fans by name"). Cited, not mirrored: the gate
// needs the fact, not the fixture.
namespace {

const std::vector<std::string> kObjectsARealK1cReports = {"fan_feedback"};

nlohmann::json load_database() {
    const std::filesystem::path db =
        std::filesystem::current_path() / "assets" / "config" / "printer_database.json";
    REQUIRE(std::filesystem::exists(db));
    std::ifstream in(db);
    REQUIRE(in.good());
    return nlohmann::json::parse(in);
}

/// Walk every heuristic in the database, whatever nesting the file uses.
void each_heuristic(const nlohmann::json& node, const std::string& owner,
                    const std::function<void(const std::string&, const nlohmann::json&)>& fn) {
    if (node.is_object()) {
        std::string next_owner = owner;
        if (node.contains("id") && node["id"].is_string())
            next_owner = node["id"].get<std::string>();
        if (node.contains("heuristics") && node["heuristics"].is_array()) {
            for (const auto& h : node["heuristics"])
                fn(next_owner, h);
        }
        for (const auto& [k, v] : node.items())
            each_heuristic(v, next_owner, fn);
    } else if (node.is_array()) {
        for (const auto& v : node)
            each_heuristic(v, owner, fn);
    }
}

} // namespace

TEST_CASE("no heuristic claims a model owns an object other hardware reports",
          "[printer_database][evidence]") {
    const nlohmann::json db = load_database();
    std::vector<std::string> offenders;

    each_heuristic(db, "", [&](const std::string& owner, const nlohmann::json& h) {
        if (!h.is_object() || !h.contains("pattern") || !h["pattern"].is_string())
            return;
        const std::string pattern = h["pattern"].get<std::string>();
        bool k1c_reports_it = false;
        for (const std::string& o : kObjectsARealK1cReports)
            if (o == pattern)
                k1c_reports_it = true;
        if (!k1c_reports_it)
            return;
        const std::string reason = h.value("reason", "");
        // "K2-specific", "K3-specific", ... anything asserting exclusivity for a
        // non-K1 model over an object a K1C demonstrably reports.
        if (reason.find("-specific") != std::string::npos &&
            reason.find("K1") == std::string::npos) {
            offenders.push_back(owner + ": pattern '" + pattern + "' reason '" + reason + "'");
        }
    });

    CAPTURE(offenders);
    CHECK(offenders.empty());
}

// The premise that makes "rewrite the reason, do not re-score" the safe call:
// fan_feedback earns a K2 entry some points against K1C-shaped hardware, but not
// enough to win, so the false claim changed no verdict. Pin that here — if a
// future re-tune makes it decisive, this fails and the reason above is the first
// thing to re-read.
TEST_CASE("a K1C fingerprint does not detect as a K2", "[printer_database][evidence]") {
    // Objects as captured from a real K1C (see the real-hardware discovery test):
    // fan_feedback present, and none of the CFS-range markers a K2 carries.
    PrinterHardwareData k1c{.heaters = {"extruder", "heater_bed"},
                            .sensors = {"temperature_sensor chamber_temp"},
                            .fans = {"fan", "temperature_fan chamber_fan"},
                            .leds = {},
                            .hostname = "K1C-A1B2",
                            .printer_objects = {"fan_feedback", "temperature_sensor chamber_temp",
                                                "temperature_fan chamber_fan", "print_stats",
                                                "virtual_sdcard", "pause_resume"},
                            .kinematics = "corexy"};

    const PrinterDetectionResult result = PrinterDetector::detect(k1c);
    CAPTURE(result.type_name, result.confidence, result.reason);
    CHECK(result.type_name.find("K2") == std::string::npos);
}

// The loader checks only that an extension file's new printers carry a name;
// nothing checks the bundled file, so a bad entry there ships silently. Image
// existence is scripts/check_printer_images.py's job.
TEST_CASE("every bundled printer entry is well-formed and uniquely named",
          "[printer_database][schema]") {
    const nlohmann::json db = load_database();
    REQUIRE(db.contains("printers"));
    REQUIRE(db["printers"].is_array());
    REQUIRE_FALSE(db["printers"].empty());

    auto lower = [](std::string s) {
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        return s;
    };

    const std::regex slug("[a-z0-9]+([_-][a-z0-9]+)*");
    std::set<std::string> ids;
    std::set<std::string> names; // lookups by name are case-insensitive
    for (const auto& printer : db["printers"]) {
        const std::string id = printer.value("id", "");
        INFO("printer id: '" << id << "'");
        for (const char* field : {"id", "name", "manufacturer", "image"}) {
            INFO("field: " << field);
            REQUIRE(printer.contains(field));
            REQUIRE(printer[field].is_string());
            CHECK_FALSE(printer[field].get<std::string>().empty());
        }
        CHECK(printer.contains("heuristics"));
        CHECK(printer.value("heuristics", nlohmann::json()).is_array());

        CHECK(std::regex_match(id, slug));
        CHECK(ids.insert(id).second);
        CHECK(names.insert(lower(printer["name"].get<std::string>())).second);
    }
}

// An image variant whose condition this build does not know never matches, so a
// misspelt key would ship a variant that silently never shows. That its image
// exists is scripts/check_printer_images.py's job.
TEST_CASE("every bundled image variant names a known condition and an image",
          "[printer_database][schema][printer_image_variants]") {
    const nlohmann::json db = load_database();
    int variants_seen = 0;
    for (const auto& printer : db["printers"]) {
        if (!printer.contains("image_variants"))
            continue;
        INFO("printer id: '" << printer.value("id", "") << "'");
        const auto& variants = printer["image_variants"];
        REQUIRE(variants.is_array());
        for (const auto& variant : variants) {
            ++variants_seen;
            INFO("variant: " << variant.dump());
            REQUIRE(variant.is_object());
            REQUIRE(variant.contains("image"));
            REQUIRE(variant["image"].is_string());
            CHECK_FALSE(variant["image"].get<std::string>().empty());
            REQUIRE(variant.contains("when"));
            REQUIRE(variant["when"].is_object());
            CHECK_FALSE(variant["when"].empty());
            for (auto it = variant["when"].begin(); it != variant["when"].end(); ++it) {
                INFO("condition: " << it.key());
                CHECK(helix::printer_images::is_known_variant_condition(it.key()));
                CHECK(
                    helix::printer_images::is_valid_variant_condition_value(it.key(), it.value()));
            }
        }
    }
    // The bundled K2 entry carries one, so a walk that finds none is reading the wrong file.
    CHECK(variants_seen > 0);
}

// A variant link that does not resolve, or a variant with no label, falls back
// to the two-printers wording that names one machine twice.
TEST_CASE("every bundled printer variant names an existing base entry and a label",
          "[printer_database][schema][type-mismatch]") {
    const nlohmann::json db = load_database();
    std::map<std::string, const nlohmann::json*> by_id;
    for (const auto& printer : db["printers"]) {
        by_id[printer.value("id", "")] = &printer;
    }
    int variants_seen = 0;
    for (const auto& printer : db["printers"]) {
        INFO("printer id: '" << printer.value("id", "") << "'");
        if (!printer.contains("variant_of")) {
            CHECK_FALSE(printer.contains("variant_label"));
            continue;
        }
        ++variants_seen;
        REQUIRE(printer["variant_of"].is_string());
        REQUIRE(printer.contains("variant_label"));
        REQUIRE(printer["variant_label"].is_string());
        CHECK_FALSE(printer["variant_label"].get<std::string>().empty());

        const std::string base_id = printer["variant_of"].get<std::string>();
        INFO("variant_of: '" << base_id << "'");
        const auto base = by_id.find(base_id);
        REQUIRE(base != by_id.end());
        CHECK(base_id != printer.value("id", ""));
        // One level only: a variant's base is the plain machine.
        CHECK_FALSE(base->second->contains("variant_of"));
    }
    CHECK(variants_seen > 0);
}
