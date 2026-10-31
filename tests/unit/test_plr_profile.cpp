// SPDX-License-Identifier: GPL-3.0-or-later
//
// The power-loss recovery profile the app writes for the HelixPrint plugin
// (include/plr_profile.h): the printer database `plr` block, the resolution
// with the user's overrides, and the record shapes the plugin reads.

#include "plr_profile.h"
#include "printer_detector.h"

#include "../catch_amalgamated.hpp"
#include "hv/json.hpp"

using helix::PlrBackendKind;
using helix::PlrDbBlock;
using helix::PlrHomingCheck;
using helix::PlrUserOverride;
using helix::StorageHint;
using json = nlohmann::json;

namespace {

constexpr PlrHomingCheck kSafeHoming{true, false, true};
constexpr PlrHomingCheck kUnsafeHoming{false, false, true};
constexpr PlrHomingCheck kUnknownHoming{};

PlrDbBlock block(int tier, bool z_holds) {
    PlrDbBlock db;
    db.tier = tier;
    db.z_holds_unpowered = z_holds;
    return db;
}

} // namespace

TEST_CASE("plr_parse_db_block: absent, full and bad-typed blocks", "[plr][profile]") {
    CHECK_FALSE(helix::plr_parse_db_block(json{{"name", "X"}}).has_value());
    CHECK_FALSE(helix::plr_parse_db_block(json{{"plr", "tier 2"}}).has_value());

    auto full = helix::plr_parse_db_block(json{{"plr",
                                                {{"tier", 2},
                                                 {"backend", "qidi_print_state"},
                                                 {"storage", "emmc"},
                                                 {"z_holds_unpowered", true},
                                                 {"xy_home_gcode", "G28 X0 Y0"},
                                                 {"z_lift_mm", 8.0},
                                                 {"purge_gcode", "PURGE"},
                                                 {"resume_skip_fraction", 0.25},
                                                 {"macro_vars", {{"M", {"a", "b"}}}}}}});
    REQUIRE(full.has_value());
    CHECK(full->tier == 2);
    CHECK(full->backend == PlrBackendKind::QidiPrintState);
    CHECK(full->storage == StorageHint::Emmc);
    CHECK(full->z_holds_unpowered);
    CHECK(full->xy_home_gcode == "G28 X0 Y0");
    CHECK(full->z_lift_mm == Catch::Approx(8.0f));
    CHECK(full->purge_gcode == std::optional<std::string>("PURGE"));
    CHECK(full->resume_skip_fraction == Catch::Approx(0.25f));
    CHECK(full->macro_vars.at("M") == std::vector<std::string>{"a", "b"});

    auto bad = helix::plr_parse_db_block(json{{"plr",
                                               {{"tier", "2"},
                                                {"backend", "marlin"},
                                                {"storage", 3},
                                                {"z_holds_unpowered", "yes"},
                                                {"xy_home_gcode", ""},
                                                {"z_lift_mm", -1},
                                                {"purge_gcode", 4},
                                                {"resume_skip_fraction", 2.0}}}});
    REQUIRE(bad.has_value());
    const PlrDbBlock defaults;
    CHECK(bad->tier == 0);
    CHECK(bad->backend == defaults.backend);
    CHECK(bad->storage == StorageHint::Unknown);
    CHECK_FALSE(bad->z_holds_unpowered);
    CHECK(bad->xy_home_gcode == "G28 X Y");
    CHECK(bad->z_lift_mm == Catch::Approx(defaults.z_lift_mm));
    CHECK_FALSE(bad->purge_gcode.has_value());
    CHECK(bad->resume_skip_fraction == Catch::Approx(0.0f));
    CHECK(helix::plr_parse_db_block(json{{"plr", {{"tier", 4}}}})->tier == 0);
}

TEST_CASE("plr_resolve_profile: tier from the database and the user", "[plr][profile]") {
    struct Row {
        std::optional<PlrDbBlock> db;
        std::optional<int> user_tier;
        std::optional<std::string> user_xy;
        PlrHomingCheck homing;
        int expected;
    };
    const Row rows[] = {
        // unlisted printer: tier 0, rule 7
        {std::nullopt, std::nullopt, std::nullopt, kSafeHoming, 0},
        {block(1, false), std::nullopt, std::nullopt, kSafeHoming, 1},
        {block(2, true), std::nullopt, std::nullopt, kSafeHoming, 2},
        // tier 2 needs Z that holds unpowered
        {block(2, false), std::nullopt, std::nullopt, kSafeHoming, 1},
        // the user raises an unlisted printer with safe homing; tier 2 needs the
        // database to say Z holds unpowered, so a request for 2 stops at 1
        {std::nullopt, 1, std::nullopt, kSafeHoming, 1},
        {std::nullopt, 2, std::nullopt, kSafeHoming, 1},
        {block(1, false), 2, std::nullopt, kSafeHoming, 1},
        {block(2, false), 2, std::nullopt, kSafeHoming, 1},
        {block(1, true), 2, std::nullopt, kSafeHoming, 2},
        // homing the app cannot judge is unsafe for a raise
        {std::nullopt, 1, std::nullopt, kUnknownHoming, 0},
        {block(1, true), 2, std::nullopt, kUnknownHoming, 0},
        // ... but never for the database's own tier, which the plugin checks
        {block(2, true), std::nullopt, std::nullopt, kUnknownHoming, 2},
        // ... with unsafe homing and no override: off
        {std::nullopt, 1, std::nullopt, kUnsafeHoming, 0},
        // ... with unsafe homing and the user's own X/Y home command
        {std::nullopt, 1, std::string("G28 X Y Z0"), kUnsafeHoming, 1},
        {std::nullopt, 1, std::nullopt, PlrHomingCheck{false, true, false}, 1},
        // lowering is always allowed
        {block(2, true), 1, std::nullopt, kUnsafeHoming, 1},
        {block(2, true), 0, std::nullopt, kSafeHoming, 0},
        // tier 3 is reserved: a user cannot select it
        {block(1, false), 3, std::nullopt, kSafeHoming, 1},
    };
    for (const auto& row : rows) {
        PlrUserOverride user;
        user.tier = row.user_tier;
        user.xy_home_gcode = row.user_xy;
        CAPTURE(row.db ? row.db->tier : -1, row.user_tier.value_or(-1), row.homing.xy_home_safe,
                row.homing.has_xy_home_override, row.user_xy.value_or(""));
        CHECK(helix::plr_resolve_profile(row.db, user, row.homing).tier == row.expected);
    }
}

TEST_CASE("plr_resolve_profile: user overrides fold into the profile", "[plr][profile]") {
    PlrDbBlock db = block(2, true);
    db.backend = PlrBackendKind::QidiPrintState;
    db.storage = StorageHint::Emmc;
    db.purge_gcode = "DB_PURGE";
    PlrUserOverride user;
    user.enabled = false;
    user.xy_home_gcode = "MY_HOME";
    user.purge_gcode = "MY_PURGE";
    user.resume_skip_fraction = 7.0f;
    auto p = helix::plr_resolve_profile(db, user, kSafeHoming);
    CHECK(p.backend == PlrBackendKind::QidiPrintState);
    CHECK(p.storage_hint == StorageHint::Emmc);
    CHECK(p.z_holds_unpowered);
    CHECK(p.xy_home_gcode == "MY_HOME");
    CHECK(p.purge_gcode == std::optional<std::string>("MY_PURGE"));
    CHECK(p.resume_skip_fraction == Catch::Approx(1.0f));
    CHECK(p.user_enabled == std::optional<bool>(false));

    auto plain = helix::plr_resolve_profile(db, PlrUserOverride{}, kSafeHoming);
    CHECK(plain.xy_home_gcode == "G28 X Y");
    CHECK(plain.purge_gcode == std::optional<std::string>("DB_PURGE"));
}

TEST_CASE("plr_profile_json and plr_user_json: the contract's shapes", "[plr][profile]") {
    PlrDbBlock db = block(2, true);
    db.backend = PlrBackendKind::QidiPrintState;
    db.storage = StorageHint::Emmc;
    db.macro_vars = {{"PRINT_START", {"bed"}}};
    json j =
        helix::plr_profile_json(helix::plr_resolve_profile(db, PlrUserOverride{}, kSafeHoming));
    CHECK(j == json{{"tier", 2},
                    {"backend", "qidi_print_state"},
                    {"storage_hint", "emmc"},
                    {"z_holds_unpowered", true},
                    {"xy_home_gcode", "G28 X Y"},
                    {"z_lift_mm", 5.0},
                    {"purge_gcode", nullptr},
                    {"resume_skip_fraction", 0.0},
                    {"macro_vars", {{"PRINT_START", {"bed"}}}}});

    json unlisted = helix::plr_profile_json(
        helix::plr_resolve_profile(std::nullopt, PlrUserOverride{}, kSafeHoming));
    CHECK(unlisted["tier"] == 0);
    CHECK(unlisted["backend"] == "generic");
    CHECK(unlisted["storage_hint"] == "unknown");
    CHECK(unlisted["macro_vars"] == json::object());

    CHECK(helix::plr_user_json(true) == json{{"enabled", true}});
}

TEST_CASE("Printer database: Qidi Q2 is listed for the plugin's print_state adapter",
          "[plr][profile]") {
    auto q2 = PrinterDetector::get_plr_block("Qidi Q2");
    REQUIRE(q2.has_value());
    CHECK(q2->backend == PlrBackendKind::QidiPrintState);
    CHECK(q2->storage == StorageHint::Emmc);
    CHECK(helix::plr_resolve_profile(q2, PlrUserOverride{}, kSafeHoming).tier == 2);
    // Creality's own recovery serves the K1 line; it stays unlisted here.
    CHECK_FALSE(PrinterDetector::get_plr_block("Creality K1C").has_value());
    CHECK_FALSE(PrinterDetector::get_plr_block("No Such Printer").has_value());
}
