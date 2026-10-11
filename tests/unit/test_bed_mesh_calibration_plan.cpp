// Copyright (C) 2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

// Where each bed mesh calibration tier stores a mesh the user named before
// probing, and what has to be copied once it finishes.

#include "bed_mesh_calibration_plan.h"
#include "printer_detector.h"

#include "../catch_amalgamated.hpp"

using helix::bed_mesh::CalibrationOutcome;
using helix::bed_mesh::check_calibration;
using helix::bed_mesh::gcode_param_value;
using helix::bed_mesh::is_profile_save_refusal;
using helix::bed_mesh::plan_calibration;
using helix::bed_mesh::profiles_replaced_by;
using helix::bed_mesh::stored_meshes_from_status;
using helix::bed_mesh::StoredMeshes;

TEST_CASE("calibration plan: the panel's own BED_MESH_CALIBRATE takes the name",
          "[bed_mesh][calibration_plan]") {
    StandardMacroInfo slot;

    SECTION("default names no profile, so Klipper writes default itself") {
        const auto plan = plan_calibration(slot, "default", 60);
        CHECK(plan.command == "BED_MESH_CALIBRATE");
        CHECK(plan.writes_profile == "default");
        CHECK(plan.copy_to.empty());
        CHECK_FALSE(plan.self_prepares);
    }

    SECTION("any other name is passed as PROFILE") {
        const auto plan = plan_calibration(slot, "cold", 60);
        CHECK(plan.command == "BED_MESH_CALIBRATE PROFILE=cold");
        CHECK(plan.writes_profile == "cold");
        CHECK(plan.copy_to.empty());
    }

    SECTION("a name with a space reaches Klipper as one parameter") {
        const auto plan = plan_calibration(slot, "PEI Sheet", 60);
        CHECK(plan.command == "BED_MESH_CALIBRATE PROFILE=\"PEI Sheet\"");
        CHECK(plan.writes_profile == "PEI Sheet");
    }

    SECTION("the conditional HELIX fallback is never what a calibration sends") {
        slot.fallback_macro = "HELIX_BED_MESH_IF_NEEDED";
        CHECK(plan_calibration(slot, "cold", 60).command == "BED_MESH_CALIBRATE PROFILE=cold");
    }
}

TEST_CASE("calibration plan: a detected or configured BED_MESH_CALIBRATE takes the name",
          "[bed_mesh][calibration_plan]") {
    StandardMacroInfo slot;

    SECTION("detected") {
        slot.detected_macro = "BED_MESH_CALIBRATE";
        const auto plan = plan_calibration(slot, "cold", 60);
        CHECK(plan.command == "BED_MESH_CALIBRATE PROFILE=cold");
        CHECK(plan.writes_profile == "cold");
    }

    SECTION("configured with arguments of its own, in any case") {
        slot.configured_macro = "bed_mesh_calibrate ADAPTIVE=1";
        const auto plan = plan_calibration(slot, "cold", 60);
        CHECK(plan.command == "bed_mesh_calibrate ADAPTIVE=1 PROFILE=cold");
        CHECK(plan.writes_profile == "cold");
        CHECK(plan.copy_to.empty());
    }

    SECTION("configured with a quoted PROFILE, read as the name Klipper stores") {
        slot.configured_macro = "BED_MESH_CALIBRATE PROFILE=\"PEI Sheet\" ADAPTIVE=1";
        auto plan = plan_calibration(slot, "cold", 60);
        CHECK(plan.writes_profile == "PEI Sheet");
        CHECK(plan.copy_to == "cold");

        slot.configured_macro = "BED_MESH_CALIBRATE PROFILE='a b'";
        plan = plan_calibration(slot, "cold", 60);
        CHECK(plan.writes_profile == "a b");

        slot.configured_macro = "BED_MESH_CALIBRATE PROFILE=\"say \\\"hi\\\"\"";
        plan = plan_calibration(slot, "cold", 60);
        CHECK(plan.writes_profile == "say \"hi\"");
    }

    SECTION("configured with a PROFILE of its own: stored there, then copied") {
        slot.configured_macro = "BED_MESH_CALIBRATE PROFILE=pei";
        const auto plan = plan_calibration(slot, "cold", 60);
        CHECK(plan.command == "BED_MESH_CALIBRATE PROFILE=pei");
        CHECK(plan.writes_profile == "pei");
        CHECK(plan.copy_to == "cold");
    }
}

TEST_CASE("calibration plan: a command that cannot be named stores default, then copies",
          "[bed_mesh][calibration_plan]") {
    StandardMacroInfo slot;

    SECTION("G29") {
        slot.detected_macro = "G29";
        const auto plan = plan_calibration(slot, "cold", 60);
        CHECK(plan.command == "G29");
        CHECK_FALSE(plan.shipped);
        CHECK(plan.writes_profile == "default");
        CHECK(plan.copy_to == "cold");
        CHECK(plan.final_profile() == "cold");
    }

    SECTION("a shipped sequence without the placeholder") {
        slot.shipped_macro = "CLEAN_NOZZLE\nBED_MESH_CALIBRATE";
        slot.shipped_self_prepares = true;
        const auto plan = plan_calibration(slot, "cold", 60);
        CHECK(plan.command == "CLEAN_NOZZLE\nBED_MESH_CALIBRATE");
        CHECK(plan.self_prepares);
        CHECK(plan.writes_profile == "default");
        CHECK(plan.copy_to == "cold");
    }

    SECTION("default needs no copy") {
        slot.detected_macro = "G29";
        const auto plan = plan_calibration(slot, "default", 60);
        CHECK(plan.copy_to.empty());
        CHECK(plan.final_profile() == "default");
    }

    SECTION("a mesh stored elsewhere is never copied into default") {
        // Klipper refuses BED_MESH_PROFILE SAVE=default.
        slot.configured_macro = "BED_MESH_CALIBRATE PROFILE=pei";
        const auto plan = plan_calibration(slot, "default", 60);
        CHECK(plan.copy_to.empty());
        CHECK(plan.final_profile() == "pei");
    }
}

TEST_CASE("calibration plan: a shipped sequence prepares itself only when its entry says so",
          "[bed_mesh][calibration_plan]") {
    StandardMacroInfo slot;
    slot.shipped_macro = "BED_MESH_CALIBRATE{profile_arg} BED_TEMP={bed_temp}";

    SECTION("without the flag the panel preheats, homes and prepares the probe") {
        const auto plan = plan_calibration(slot, "default", 60);
        CHECK(plan.command == "BED_MESH_CALIBRATE BED_TEMP=60");
        CHECK(plan.shipped);
        CHECK_FALSE(plan.self_prepares);
    }

    SECTION("with the flag the sequence is sent alone") {
        slot.shipped_self_prepares = true;
        const auto plan = plan_calibration(slot, "default", 60);
        CHECK(plan.shipped);
        CHECK(plan.self_prepares);
    }
}

TEST_CASE("calibration plan: the Centauri Carbon template, default and named",
          "[bed_mesh][calibration_plan][cc1]") {
    PrinterDetector::reload();
    StandardMacroInfo slot;
    slot.shipped_macro = PrinterDetector::get_bed_mesh_calibrate_gcode("Elegoo Centauri Carbon");
    slot.shipped_self_prepares =
        PrinterDetector::get_bed_mesh_self_prepares("Elegoo Centauri Carbon");
    REQUIRE_FALSE(slot.shipped_macro.empty());
    REQUIRE(slot.shipped_self_prepares);

    SECTION("default") {
        const auto plan = plan_calibration(slot, "default", 60);
        CHECK(plan.command == "BED_MESH_CALIBRATE BED_TEMP=60");
        CHECK(plan.writes_profile == "default");
        CHECK(plan.copy_to.empty());
        CHECK(plan.self_prepares);
        CHECK(plan.shipped);
    }

    SECTION("named") {
        const auto plan = plan_calibration(slot, "cold", 95);
        CHECK(plan.command == "BED_MESH_CALIBRATE PROFILE=cold BED_TEMP=95");
        CHECK(plan.writes_profile == "cold");
        CHECK(plan.copy_to.empty());
    }
}

TEST_CASE("profile commands carry the name as one parameter", "[bed_mesh][calibration_plan]") {
    using helix::bed_mesh::profile_command;
    CHECK(profile_command("LOAD", "cold") == "BED_MESH_PROFILE LOAD=cold");
    CHECK(profile_command("REMOVE", "PEI Sheet") == "BED_MESH_PROFILE REMOVE=\"PEI Sheet\"");
}

TEST_CASE("gcode_param_value quotes only what Klipper would split",
          "[bed_mesh][calibration_plan]") {
    CHECK(gcode_param_value("cold") == "cold");
    CHECK(gcode_param_value("PEI Sheet") == "\"PEI Sheet\"");
    CHECK(gcode_param_value("a#b") == "\"a#b\"");
    CHECK(gcode_param_value("say \"hi\"") == "\"say \\\"hi\\\"\"");
}

TEST_CASE("profile save refusals are recognised in the firmware's own words",
          "[bed_mesh][calibration_plan]") {
    // Klipper's bed_mesh.py, as notify_gcode_response delivers it.
    CHECK(is_profile_save_refusal(
        "// Unable to save to profile [cold], the bed has not been probed"));
    CHECK(is_profile_save_refusal(
        "// Profile 'default' is reserved, please choose another profile name."));
    CHECK_FALSE(is_profile_save_refusal("// Bed Mesh state has been saved to profile [cold]"));
    CHECK_FALSE(is_profile_save_refusal("// Mesh Bed Leveling Complete"));
}

TEST_CASE("stored meshes are read from the bed_mesh status object",
          "[bed_mesh][calibration_plan]") {
    const auto status = nlohmann::json::parse(R"({
        "profile_name": "default",
        "profiles": {
            "default": {"points": [[0.1, 0.2], [0.3, 0.4]], "mesh_params": {"x_count": 2}},
            "cold": {"mesh_params": {"x_count": 2}}
        }
    })");
    const StoredMeshes meshes = stored_meshes_from_status(status);
    REQUIRE(meshes.size() == 2);
    CHECK(meshes.at("default") == nlohmann::json::parse("[[0.1, 0.2], [0.3, 0.4]]"));
    CHECK(meshes.at("cold").is_null());

    CHECK(stored_meshes_from_status(nlohmann::json::object()).empty());
    CHECK(stored_meshes_from_status(nlohmann::json()).empty());
}

namespace {

const nlohmann::json OLD_POINTS = nlohmann::json::parse("[[0.1, 0.2]]");
const nlohmann::json NEW_POINTS = nlohmann::json::parse("[[0.15, 0.25]]");

helix::bed_mesh::CalibrationPlan plan_for(const char* detected, const char* configured,
                                          const char* name) {
    StandardMacroInfo slot;
    slot.detected_macro = detected;
    slot.configured_macro = configured;
    return plan_calibration(slot, name, 60);
}

} // namespace

TEST_CASE("calibration check: a mesh is trusted only where a profile changed",
          "[bed_mesh][calibration_plan]") {
    SECTION("probed straight into the chosen profile, new or re-probed") {
        const auto plan = plan_for("BED_MESH_CALIBRATE", "", "cold");
        REQUIRE(plan.writes_profile == "cold");

        auto r = check_calibration(plan, {}, {{"cold", NEW_POINTS}});
        CHECK(r.outcome == CalibrationOutcome::Stored);
        CHECK(r.to == "cold");

        r = check_calibration(plan, {{"cold", OLD_POINTS}}, {{"cold", NEW_POINTS}});
        CHECK(r.outcome == CalibrationOutcome::Stored);
        CHECK(r.to == "cold");
    }

    SECTION("a command that dropped its PROFILE stored in default, which is copied") {
        const auto plan = plan_for("BED_MESH_CALIBRATE", "", "cold");
        const StoredMeshes before{{"default", OLD_POINTS}, {"cold", OLD_POINTS}};
        const StoredMeshes after{{"default", NEW_POINTS}, {"cold", OLD_POINTS}};
        const auto r = check_calibration(plan, before, after);
        CHECK(r.outcome == CalibrationOutcome::Copy);
        CHECK(r.from == "default");
        CHECK(r.to == "cold");
    }

    SECTION("the copy plan copies only a default that changed") {
        const auto plan = plan_for("G29", "", "cold");
        REQUIRE(plan.copy_to == "cold");

        auto r = check_calibration(plan, {{"default", OLD_POINTS}}, {{"default", NEW_POINTS}});
        CHECK(r.outcome == CalibrationOutcome::Copy);
        CHECK(r.from == "default");
        CHECK(r.to == "cold");

        r = check_calibration(plan, {{"default", OLD_POINTS}}, {{"default", OLD_POINTS}});
        CHECK(r.outcome == CalibrationOutcome::NotStored);
    }

    SECTION("nothing changed anywhere: not stored, whatever the profiles hold") {
        const auto plan = plan_for("BED_MESH_CALIBRATE", "", "cold");
        const StoredMeshes same{{"default", OLD_POINTS}, {"cold", OLD_POINTS}};
        CHECK(check_calibration(plan, same, same).outcome == CalibrationOutcome::NotStored);
        CHECK(check_calibration(plan, {}, {}).outcome == CalibrationOutcome::NotStored);
    }

    SECTION("a profile that vanished is not a stored mesh") {
        const auto plan = plan_for("BED_MESH_CALIBRATE", "", "cold");
        CHECK(check_calibration(plan, {{"cold", OLD_POINTS}}, {}).outcome ==
              CalibrationOutcome::NotStored);
    }

    SECTION("chosen default, command stores elsewhere but dropped that: it is in default") {
        const auto plan = plan_for("", "BED_MESH_CALIBRATE PROFILE=pei", "default");
        REQUIRE(plan.writes_profile == "pei");
        const auto r = check_calibration(plan, {{"pei", OLD_POINTS}, {"default", OLD_POINTS}},
                                         {{"pei", OLD_POINTS}, {"default", NEW_POINTS}});
        CHECK(r.outcome == CalibrationOutcome::Stored);
        CHECK(r.to == "default");
    }
}

TEST_CASE("replacement check: default is re-probed freely, anything else is asked about",
          "[bed_mesh][calibration_plan]") {
    const std::vector<std::string> stored{"default", "cold"};

    CHECK(profiles_replaced_by(plan_for("BED_MESH_CALIBRATE", "", "default"), stored).empty());
    CHECK(profiles_replaced_by(plan_for("BED_MESH_CALIBRATE", "", "warm"), stored).empty());
    CHECK(profiles_replaced_by(plan_for("BED_MESH_CALIBRATE", "", "cold"), stored) ==
          std::vector<std::string>{"cold"});

    // A command that stores in default first replaces it on the way to the chosen name.
    CHECK(profiles_replaced_by(plan_for("G29", "", "warm"), stored) ==
          std::vector<std::string>{"default"});
    CHECK(profiles_replaced_by(plan_for("G29", "", "cold"), stored) ==
          (std::vector<std::string>{"cold", "default"}));
    CHECK(profiles_replaced_by(plan_for("G29", "", "default"), stored).empty());
    // Nothing stored there yet, nothing to replace.
    CHECK(profiles_replaced_by(plan_for("G29", "", "warm"), {}).empty());
}
