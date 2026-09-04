// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Pure tests for the two-backend Power-Loss-Recovery strategy
// (include/plr_backend.h). No LVGL, no network, no singletons.
//
// The safety-critical property under test is the CREALITY resume invariant:
// the `pause_resume/check_continue_print_state` probe is what sets
// print_stats.power_loss = 1 in firmware, and the stock sensorless-homing macro
// gates its pre-homing Z clearance lift on that flag. Resuming without a
// completed probe makes the machine lift 0.1mm and sensorless-home X/Y through
// a tall part. plr_build_plan() is where that invariant is enforced, so every
// path that could produce a resume gcode is pinned here.

#include "plr_backend.h"

#include "../catch_amalgamated.hpp"
#include "hv/json.hpp"

using helix::PlrBackendType;
using helix::PlrCapabilitySignals;
using helix::PlrDetectResult;
using helix::PlrRecoveryPlan;
using json = nlohmann::json;

namespace {

/// A probe that ran and confirmed both halves — the only state that may
/// authorize a Creality resume.
PlrDetectResult confirmed_detect() {
    PlrDetectResult r;
    r.completed = true;
    r.file_state = true;
    r.eeprom_state = true;
    return r;
}

} // namespace

// ===========================================================================
// Backend selection
// ===========================================================================

TEST_CASE("plr_select_backend: no capability markers => NONE", "[plr][backend]") {
    PlrCapabilitySignals caps;
    REQUIRE(helix::plr_select_backend(caps) == PlrBackendType::NONE);
}

TEST_CASE("plr_select_backend: print_stats.power_loss present => CREALITY", "[plr][backend]") {
    PlrCapabilitySignals caps;
    caps.creality_power_loss_field = true;
    REQUIRE(helix::plr_select_backend(caps) == PlrBackendType::CREALITY);
}

TEST_CASE("plr_select_backend: power_loss key absent => never CREALITY", "[plr][backend]") {
    // Mainline Klipper has no power_loss key at all; Moonraker answers the
    // subscribed field with an explicit null, which the parser must NOT treat
    // as presence. Selecting CREALITY here would fire a side-effectful probe at
    // a printer whose firmware has no such endpoint.
    PlrCapabilitySignals caps;
    caps.creality_power_loss_field = false;
    caps.snapmaker_pl_env_valid = false;
    REQUIRE(helix::plr_select_backend(caps) == PlrBackendType::NONE);
}

TEST_CASE("plr_select_backend: pl_env_valid => SNAPMAKER", "[plr][backend]") {
    PlrCapabilitySignals caps;
    caps.snapmaker_pl_env_valid = true;
    REQUIRE(helix::plr_select_backend(caps) == PlrBackendType::SNAPMAKER);
}

TEST_CASE("plr_select_backend: both markers => SNAPMAKER wins (no probe needed)",
          "[plr][backend]") {
    PlrCapabilitySignals caps;
    caps.snapmaker_pl_env_valid = true;
    caps.creality_power_loss_field = true;
    REQUIRE(helix::plr_select_backend(caps) == PlrBackendType::SNAPMAKER);
}

// ===========================================================================
// Creality availability — BOTH states required
// ===========================================================================

TEST_CASE("plr_creality_recovery_available: both states true after a completed probe",
          "[plr][backend][creality]") {
    REQUIRE(helix::plr_creality_recovery_available(confirmed_detect()) == true);
}

TEST_CASE("plr_creality_recovery_available: file_state only => unavailable",
          "[plr][backend][creality]") {
    PlrDetectResult r = confirmed_detect();
    r.eeprom_state = false;
    // Without the bl24c16f EEPROM snapshot, ISCONTINUEPRINT=1 silently restarts
    // the print from the beginning. This gate is what prevents that.
    REQUIRE(helix::plr_creality_recovery_available(r) == false);
}

TEST_CASE("plr_creality_recovery_available: eeprom_state only => unavailable",
          "[plr][backend][creality]") {
    PlrDetectResult r = confirmed_detect();
    r.file_state = false;
    REQUIRE(helix::plr_creality_recovery_available(r) == false);
}

TEST_CASE("plr_creality_recovery_available: both true but probe never completed => unavailable",
          "[plr][backend][creality]") {
    PlrDetectResult r;
    r.completed = false;
    r.file_state = true;
    r.eeprom_state = true;
    REQUIRE(helix::plr_creality_recovery_available(r) == false);
}

TEST_CASE("plr_creality_recovery_available: idle printer response (both false)",
          "[plr][backend][creality]") {
    // Verbatim shape of a live idle K1C: {"result":{"file_state":false,"eeprom_state":false}}
    PlrDetectResult r;
    r.completed = true;
    REQUIRE(helix::plr_creality_recovery_available(r) == false);
}

// ===========================================================================
// Probe response parsing
// ===========================================================================

TEST_CASE("plr_parse_check_continue_response: idle K1C response parses",
          "[plr][backend][creality]") {
    json response = {{"result", {{"file_state", false}, {"eeprom_state", false}}}};
    PlrDetectResult r;
    REQUIRE(helix::plr_parse_check_continue_response(response, r) == true);
    REQUIRE(r.completed == true);
    REQUIRE(r.file_state == false);
    REQUIRE(r.eeprom_state == false);
}

TEST_CASE("plr_parse_check_continue_response: recoverable response parses",
          "[plr][backend][creality]") {
    json response = {{"result", {{"file_state", true}, {"eeprom_state", true}}}};
    PlrDetectResult r;
    REQUIRE(helix::plr_parse_check_continue_response(response, r) == true);
    REQUIRE(r.completed == true);
    REQUIRE(r.file_state == true);
    REQUIRE(r.eeprom_state == true);
}

TEST_CASE("plr_parse_check_continue_response: missing result => not completed",
          "[plr][backend][creality]") {
    json response = {{"error", {{"message", "Unknown method"}}}};
    PlrDetectResult r;
    REQUIRE(helix::plr_parse_check_continue_response(response, r) == false);
    REQUIRE(r.completed == false);
}

TEST_CASE("plr_parse_check_continue_response: non-boolean fields => not completed",
          "[plr][backend][creality]") {
    // A firmware that answers with strings must not be read as a confirmation.
    json response = {{"result", {{"file_state", "true"}, {"eeprom_state", 1}}}};
    PlrDetectResult r;
    REQUIRE(helix::plr_parse_check_continue_response(response, r) == false);
    REQUIRE(r.completed == false);
}

TEST_CASE("plr_parse_check_continue_response: half a payload => not completed",
          "[plr][backend][creality]") {
    json response = {{"result", {{"file_state", true}}}};
    PlrDetectResult r;
    REQUIRE(helix::plr_parse_check_continue_response(response, r) == false);
    REQUIRE(r.completed == false);
}

// ===========================================================================
// Plan building — Snapmaker (must be byte-identical to the pre-refactor gcode)
// ===========================================================================

TEST_CASE("plr_build_plan: SNAPMAKER uses the fork's restore/clear gcode", "[plr][backend]") {
    PlrRecoveryPlan plan =
        helix::plr_build_plan(PlrBackendType::SNAPMAKER, "interrupted.gcode", PlrDetectResult{});
    REQUIRE(plan.backend == PlrBackendType::SNAPMAKER);
    REQUIRE(plan.resume_gcode == "SDCARD_PRINT_PL_RESTORE");
    REQUIRE(plan.discard_gcode == "SDCARD_PRINT_PL_CLEAR_ENV");
    REQUIRE(plan.discard_rpc_method.empty());
    REQUIRE(plan.recovery_file == "interrupted.gcode");
    REQUIRE(plan.resume_allowed() == true);
}

TEST_CASE("plr_build_plan: SNAPMAKER does not depend on the Creality probe", "[plr][backend]") {
    // The Snapmaker path is passive — pl_env_valid already means "validated
    // snapshot". Requiring a probe there would break every U1.
    PlrDetectResult never_probed;
    PlrRecoveryPlan plan = helix::plr_build_plan(PlrBackendType::SNAPMAKER, "", never_probed);
    REQUIRE(plan.resume_allowed() == true);
    REQUIRE(plan.resume_gcode == "SDCARD_PRINT_PL_RESTORE");
    REQUIRE(plan.recovery_file.empty()); // prompt degrades to the generic body
}

// ===========================================================================
// Plan building — Creality, including the resume-without-detect invariant
// ===========================================================================

TEST_CASE("plr_build_plan: CREALITY builds SDCARD_PRINT_FILE with ISCONTINUEPRINT=1",
          "[plr][backend][creality]") {
    PlrRecoveryPlan plan =
        helix::plr_build_plan(PlrBackendType::CREALITY, "benchy.gcode", confirmed_detect());
    REQUIRE(plan.backend == PlrBackendType::CREALITY);
    REQUIRE(plan.resume_gcode == "SDCARD_PRINT_FILE FILENAME=\"benchy.gcode\" ISCONTINUEPRINT=1");
    REQUIRE(plan.discard_rpc_method == "printer.pause_resume.cancel_continue_print");
    REQUIRE(plan.discard_gcode.empty());
    REQUIRE(plan.resume_allowed() == true);
}

TEST_CASE("plr_build_plan: CREALITY refuses resume when the probe never ran",
          "[plr][backend][creality]") {
    // THE invariant. The probe is what sets print_stats.power_loss=1; the stock
    // sensorless-homing macro reads that flag to decide between a full Z
    // clearance lift and a 0.1mm lift. Resuming without it homes X/Y through
    // the part.
    PlrDetectResult never_probed;
    PlrRecoveryPlan plan =
        helix::plr_build_plan(PlrBackendType::CREALITY, "benchy.gcode", never_probed);
    REQUIRE(plan.resume_gcode.empty());
    REQUIRE(plan.resume_allowed() == false);
    // Discard is still safe to expose — it touches no motion.
    REQUIRE(plan.discard_rpc_method == "printer.pause_resume.cancel_continue_print");
}

TEST_CASE("plr_build_plan: CREALITY refuses resume on a half-confirmed probe",
          "[plr][backend][creality]") {
    PlrDetectResult half = confirmed_detect();
    half.eeprom_state = false;
    REQUIRE(
        helix::plr_build_plan(PlrBackendType::CREALITY, "benchy.gcode", half).resume_allowed() ==
        false);

    PlrDetectResult other_half = confirmed_detect();
    other_half.file_state = false;
    REQUIRE(helix::plr_build_plan(PlrBackendType::CREALITY, "benchy.gcode", other_half)
                .resume_allowed() == false);
}

TEST_CASE("plr_build_plan: CREALITY refuses resume without a filename",
          "[plr][backend][creality]") {
    // The gcode embeds FILENAME=; with nothing to substitute there is no safe
    // command to send at all.
    PlrRecoveryPlan plan = helix::plr_build_plan(PlrBackendType::CREALITY, "", confirmed_detect());
    REQUIRE(plan.resume_allowed() == false);
}

TEST_CASE("plr_build_plan: CREALITY refuses an injection-shaped filename",
          "[plr][backend][creality]") {
    PlrRecoveryPlan plan =
        helix::plr_build_plan(PlrBackendType::CREALITY, "a.gcode\nM112", confirmed_detect());
    REQUIRE(plan.resume_allowed() == false);
}

TEST_CASE("plr_build_plan: CREALITY quotes a filename containing spaces",
          "[plr][backend][creality]") {
    // Klipper tokenizes extended parameters with shlex in POSIX mode, so an
    // unquoted "my part v2.gcode" splits into three tokens, two of which carry
    // no '=' — Klipper answers "Malformed command args ... not enough values to
    // unpack". Spaces are the common case, not the exotic one: slicers put the
    // model name and filament name in the filename.
    PlrRecoveryPlan plan =
        helix::plr_build_plan(PlrBackendType::CREALITY, "my part v2.gcode", confirmed_detect());
    REQUIRE(plan.resume_allowed() == true);
    REQUIRE(plan.resume_gcode ==
            "SDCARD_PRINT_FILE FILENAME=\"my part v2.gcode\" ISCONTINUEPRINT=1");
}

TEST_CASE("plr_build_plan: CREALITY sends the sidecar path relative and quoted",
          "[plr][backend][creality]") {
    // Verbatim shape of the field report: the sidecar hands back an absolute
    // path and the model name contains spaces. Both have to be handled — the
    // quoting so Klipper's shlex tokenizer keeps the name in one piece, the
    // root stripping so virtual_sdcard's relative file list actually matches.
    const std::string path =
        "/usr/data/printer_data/gcodes/PTOP Phone Stand_Elegoo PLA Matte Slate Grey_1h55m.gcode";
    PlrRecoveryPlan plan =
        helix::plr_build_plan(PlrBackendType::CREALITY, path, confirmed_detect());
    REQUIRE(plan.resume_allowed() == true);
    REQUIRE(plan.resume_gcode ==
            "SDCARD_PRINT_FILE FILENAME=\"PTOP Phone Stand_Elegoo PLA Matte Slate "
            "Grey_1h55m.gcode\" ISCONTINUEPRINT=1");
    // The prompt body still shows what the sidecar actually said.
    REQUIRE(plan.recovery_file == path);
}

// ===========================================================================
// Sidecar path -> SDCARD_PRINT_FILE name
// ===========================================================================

TEST_CASE("plr_creality_sdcard_relative_name: strips the known data roots", "[plr][backend]") {
    REQUIRE(helix::plr_creality_sdcard_relative_name("/usr/data/printer_data/gcodes/a.gcode") ==
            "a.gcode");
    REQUIRE(helix::plr_creality_sdcard_relative_name("/mnt/UDISK/printer_data/gcodes/a.gcode") ==
            "a.gcode");
}

TEST_CASE("plr_creality_sdcard_relative_name: keeps subdirectories", "[plr][backend]") {
    // check_subdirs=True, and the file list stores "sub/a.gcode" — dropping the
    // subdirectory would miss just as badly as leaving the root on.
    REQUIRE(helix::plr_creality_sdcard_relative_name(
                "/usr/data/printer_data/gcodes/sub dir/a.gcode") == "sub dir/a.gcode");
    // A job genuinely stored in a folder called "gcodes" keeps it: the marker
    // search takes the FIRST segment, not the last.
    REQUIRE(helix::plr_creality_sdcard_relative_name("/opt/printer_data/gcodes/gcodes/a.gcode") ==
            "gcodes/a.gcode");
}

TEST_CASE("plr_creality_sdcard_relative_name: relative input passes through", "[plr][backend]") {
    REQUIRE(helix::plr_creality_sdcard_relative_name("a.gcode") == "a.gcode");
    REQUIRE(helix::plr_creality_sdcard_relative_name("sub/a.gcode") == "sub/a.gcode");
    REQUIRE(helix::plr_creality_sdcard_relative_name("").empty());
}

TEST_CASE("plr_creality_sdcard_relative_name: unrecognizable root left alone", "[plr][backend]") {
    // Nothing safe to strip. Sending it unchanged fails loudly with "Unable to
    // open file" rather than silently addressing the wrong job.
    REQUIRE(helix::plr_creality_sdcard_relative_name("/somewhere/else/a.gcode") ==
            "/somewhere/else/a.gcode");
}

TEST_CASE("plr_build_plan: NONE backend yields no actions at all", "[plr][backend]") {
    PlrRecoveryPlan plan =
        helix::plr_build_plan(PlrBackendType::NONE, "benchy.gcode", confirmed_detect());
    REQUIRE(plan.resume_allowed() == false);
    REQUIRE(plan.discard_available() == false);
}

// ===========================================================================
// Filename safety predicate
// ===========================================================================

TEST_CASE("plr_is_safe_recovery_filename: ordinary names accepted", "[plr][backend]") {
    REQUIRE(helix::plr_is_safe_recovery_filename("benchy.gcode") == true);
    REQUIRE(helix::plr_is_safe_recovery_filename("gcodes/sub dir/part_v2.gcode") == true);
    REQUIRE(helix::plr_is_safe_recovery_filename("Häuschen.gcode") == true);
}

TEST_CASE("plr_is_safe_recovery_filename: gcode terminators rejected", "[plr][backend]") {
    REQUIRE(helix::plr_is_safe_recovery_filename("") == false);
    REQUIRE(helix::plr_is_safe_recovery_filename("a\nM112") == false);
    REQUIRE(helix::plr_is_safe_recovery_filename("a\rM112") == false);
    REQUIRE(helix::plr_is_safe_recovery_filename("a;comment") == false);
    REQUIRE(helix::plr_is_safe_recovery_filename("a*checksum") == false);
    REQUIRE(helix::plr_is_safe_recovery_filename("a#hash") == false);
    // '=' would start a new extended parameter, truncating the filename.
    REQUIRE(helix::plr_is_safe_recovery_filename("a=b.gcode") == false);
}

TEST_CASE("plr_is_safe_recovery_filename: shlex quoting characters rejected", "[plr][backend]") {
    // The value is emitted inside double quotes, so a '"' closes it early and
    // everything after becomes fresh parameters; '\' is shlex's POSIX escape.
    REQUIRE(helix::plr_is_safe_recovery_filename("a\" ISCONTINUEPRINT=0 X=\"b") == false);
    REQUIRE(helix::plr_is_safe_recovery_filename("a\\b.gcode") == false);
}

// ===========================================================================
// Sidecar parsing (best-effort filename source)
// ===========================================================================

TEST_CASE("plr_parse_creality_sidecar: file_path extracted", "[plr][backend][creality]") {
    REQUIRE(helix::plr_parse_creality_sidecar(
                R"({"file_path":"/usr/data/printer_data/gcodes/a.gcode"})") ==
            "/usr/data/printer_data/gcodes/a.gcode");
}

TEST_CASE("plr_parse_creality_sidecar: extra keys ignored", "[plr][backend][creality]") {
    REQUIRE(helix::plr_parse_creality_sidecar(
                R"({"print_file_name":"a","file_path":"b.gcode","x":1})") == "b.gcode");
}

TEST_CASE("plr_parse_creality_sidecar: missing key => empty", "[plr][backend][creality]") {
    REQUIRE(helix::plr_parse_creality_sidecar(R"({"print_file_name":"a"})").empty());
}

TEST_CASE("plr_parse_creality_sidecar: non-string file_path => empty", "[plr][backend][creality]") {
    REQUIRE(helix::plr_parse_creality_sidecar(R"({"file_path":123})").empty());
}

TEST_CASE("plr_parse_creality_sidecar: malformed JSON => empty, no throw",
          "[plr][backend][creality]") {
    REQUIRE_NOTHROW(helix::plr_parse_creality_sidecar("{not json"));
    REQUIRE(helix::plr_parse_creality_sidecar("{not json").empty());
    REQUIRE(helix::plr_parse_creality_sidecar("").empty());
}
