// SPDX-License-Identifier: GPL-3.0-or-later

#include "z_offset_utils.h"

#include <cstring>

#include "../catch_amalgamated.hpp"

using namespace helix::zoffset;
using helix::ZOffsetCalibrationStrategy;

// ============================================================================
// format_delta tests
// ============================================================================

TEST_CASE("format_delta: zero microns produces empty string", "[zoffset][format]") {
    char buf[32] = "garbage";
    format_delta(0, buf, sizeof(buf));
    REQUIRE(buf[0] == '\0');
}

TEST_CASE("format_delta: positive microns formats with plus sign", "[zoffset][format]") {
    char buf[32] = {};
    format_delta(50, buf, sizeof(buf));
    REQUIRE(std::string(buf) == "+0.050mm");
}

TEST_CASE("format_delta: negative microns formats with minus sign", "[zoffset][format]") {
    char buf[32] = {};
    format_delta(-25, buf, sizeof(buf));
    REQUIRE(std::string(buf) == "-0.025mm");
}

TEST_CASE("format_delta: large positive value", "[zoffset][format]") {
    char buf[32] = {};
    format_delta(1500, buf, sizeof(buf));
    REQUIRE(std::string(buf) == "+1.500mm");
}

// ============================================================================
// format_offset tests
// ============================================================================

TEST_CASE("format_offset: zero microns shows +0.000mm", "[zoffset][format]") {
    char buf[32] = {};
    format_offset(0, buf, sizeof(buf));
    REQUIRE(std::string(buf) == "+0.000mm");
}

TEST_CASE("format_offset: positive microns", "[zoffset][format]") {
    char buf[32] = {};
    format_offset(100, buf, sizeof(buf));
    REQUIRE(std::string(buf) == "+0.100mm");
}

TEST_CASE("format_offset: negative microns", "[zoffset][format]") {
    char buf[32] = {};
    format_offset(-250, buf, sizeof(buf));
    REQUIRE(std::string(buf) == "-0.250mm");
}

// ============================================================================
// is_auto_saved tests
// ============================================================================

TEST_CASE("is_auto_saved: FIRMWARE_MANAGED returns true", "[zoffset][strategy]") {
    REQUIRE(is_auto_saved(ZOffsetCalibrationStrategy::FIRMWARE_MANAGED) == true);
}

TEST_CASE("is_auto_saved: PROBE_CALIBRATE returns false", "[zoffset][strategy]") {
    REQUIRE(is_auto_saved(ZOffsetCalibrationStrategy::PROBE_CALIBRATE) == false);
}

TEST_CASE("is_auto_saved: ENDSTOP returns false", "[zoffset][strategy]") {
    REQUIRE(is_auto_saved(ZOffsetCalibrationStrategy::ENDSTOP) == false);
}

// ============================================================================
// should_extend_save_timeout tests
// ============================================================================
//
// Regression cover for the Creality K2 + CFS save path: SAVE_CONFIG restarts
// Klipper, and Creality's motor_control_wrapper.py chains a second config write
// (CFS Tn_data via CXSAVE_CONFIG) ~50s later. A fixed 30s SAVING guard fired
// mid-restart and flipped the panel to ERROR with "Z-offset calibration timed
// out" even though the save had succeeded.

TEST_CASE("should_extend_save_timeout: extends while a restart is expected",
          "[zoffset][save_timeout]") {
    // This is the K2/CFS case — the guard must NOT fail the operation.
    REQUIRE(should_extend_save_timeout(/*expected_restart=*/true, /*extensions_used=*/0,
                                       /*max_extensions=*/4));
    REQUIRE(should_extend_save_timeout(true, 3, 4));
}

TEST_CASE("should_extend_save_timeout: does not extend when no restart is expected",
          "[zoffset][save_timeout]") {
    // Genuinely hung save — a real timeout must still be reported. If this
    // returned true the guard would be effectively deleted.
    REQUIRE_FALSE(should_extend_save_timeout(/*expected_restart=*/false, 0, 4));
    REQUIRE_FALSE(should_extend_save_timeout(false, 3, 4));
}

TEST_CASE("should_extend_save_timeout: extension budget is bounded", "[zoffset][save_timeout]") {
    // Even with a restart perpetually 'expected', extensions must run out so the
    // panel cannot spin forever.
    REQUIRE_FALSE(should_extend_save_timeout(true, 4, 4));
    REQUIRE_FALSE(should_extend_save_timeout(true, 5, 4));
    REQUIRE_FALSE(should_extend_save_timeout(true, 99, 4));
}

TEST_CASE("should_extend_save_timeout: zero budget never extends", "[zoffset][save_timeout]") {
    REQUIRE_FALSE(should_extend_save_timeout(true, 0, 0));
}

// ============================================================================
// SaveRestartLatch tests
// ============================================================================
//
// Observed K2 timeline this models:
//   t=0    SAVE_CONFIG sent, panel enters SAVING, Klipper begins restarting
//   t~15s  klippy back READY  -> save succeeded, panel must settle here
//   t~53s  chained CXSAVE_CONFIG restart (already settled; not our problem)
//
// The latch exists because sampling is_expected_restart() at guard-fire time
// (t=30s) always reads false — the 15s suppression window has closed — so the
// extension gate never opened and the panel failed a save that had succeeded.

TEST_CASE("SaveRestartLatch: starts clean", "[zoffset][save_latch]") {
    helix::zoffset::SaveRestartLatch latch;
    REQUIRE_FALSE(latch.restart_latched());
    REQUIRE_FALSE(latch.restart_completed());
}

TEST_CASE("SaveRestartLatch: latches when klippy leaves READY", "[zoffset][save_latch]") {
    helix::zoffset::SaveRestartLatch latch;
    latch.on_klippy_ready(false); // SAVE_CONFIG restart begins
    REQUIRE(latch.restart_latched());
    // Restart began but has not finished — not yet evidence of success.
    REQUIRE_FALSE(latch.restart_completed());
}

TEST_CASE("SaveRestartLatch: latch survives until reset (the whole point)",
          "[zoffset][save_latch]") {
    helix::zoffset::SaveRestartLatch latch;
    latch.on_klippy_ready(false);
    latch.on_klippy_ready(true);

    // At guard-fire time, long after the restart settled and the suppression
    // window closed, the latch must STILL report the restart. An instantaneous
    // is_expected_restart() sample reads false here — that was the inert bug.
    REQUIRE(latch.restart_latched());
    REQUIRE(should_extend_save_timeout(latch.restart_latched(), 0, 4));
}

TEST_CASE("SaveRestartLatch: READY after a restart signals save success", "[zoffset][save_latch]") {
    helix::zoffset::SaveRestartLatch latch;
    latch.on_klippy_ready(false); // t=0 restart begins
    REQUIRE_FALSE(latch.restart_completed());

    latch.on_klippy_ready(true); // t~15s Klipper back
    // This is what lets the panel settle at ~15s instead of burning 4x30s of
    // extensions and then failing a save that worked.
    REQUIRE(latch.restart_completed());
}

TEST_CASE("SaveRestartLatch: READY without a preceding restart is not success",
          "[zoffset][save_latch]") {
    helix::zoffset::SaveRestartLatch latch;
    // Klipper was READY all along — the save never restarted anything, so a
    // READY sample must not be mistaken for a completed save.
    latch.on_klippy_ready(true);
    latch.on_klippy_ready(true);
    REQUIRE_FALSE(latch.restart_latched());
    REQUIRE_FALSE(latch.restart_completed());
}

TEST_CASE("SaveRestartLatch: note_restart_expected folds in the suppression window",
          "[zoffset][save_latch]") {
    helix::zoffset::SaveRestartLatch latch;
    latch.note_restart_expected(false);
    REQUIRE_FALSE(latch.restart_latched());

    latch.note_restart_expected(true);
    REQUIRE(latch.restart_latched());

    // Monotonic within a save — a later false must not clear it.
    latch.note_restart_expected(false);
    REQUIRE(latch.restart_latched());
}

TEST_CASE("SaveRestartLatch: reset clears both flags for a second save", "[zoffset][save_latch]") {
    helix::zoffset::SaveRestartLatch latch;
    latch.on_klippy_ready(false);
    latch.on_klippy_ready(true);
    REQUIRE(latch.restart_latched());
    REQUIRE(latch.restart_completed());

    latch.reset();
    REQUIRE_FALSE(latch.restart_latched());
    REQUIRE_FALSE(latch.restart_completed());
}

TEST_CASE("SaveRestartLatch: a second save does not inherit the first save's latch",
          "[zoffset][save_latch]") {
    // Real failure mode: a sticky latch would make save #2 immediately look like
    // it had restarted Klipper, so a genuinely hung second save would extend its
    // timeout instead of failing, and a stray READY would report false success.
    helix::zoffset::SaveRestartLatch latch;

    // Save #1: restarts and completes.
    latch.on_klippy_ready(false);
    latch.on_klippy_ready(true);
    REQUIRE(latch.restart_completed());

    // Save #2 begins (panel re-enters SAVING -> reset).
    latch.reset();

    // Klipper never dips; save #2 hangs. Must NOT extend, must NOT look complete.
    latch.on_klippy_ready(true);
    REQUIRE_FALSE(latch.restart_completed());
    REQUIRE_FALSE(latch.restart_latched());
    REQUIRE_FALSE(should_extend_save_timeout(latch.restart_latched(), 0, 4));
}

TEST_CASE("SaveRestartLatch: hung save with no restart still fails terminally",
          "[zoffset][save_latch]") {
    helix::zoffset::SaveRestartLatch latch;
    // Nothing observed at all — the genuinely-hung case.
    REQUIRE_FALSE(should_extend_save_timeout(latch.restart_latched(), 0, 4));
}

TEST_CASE("SaveRestartLatch: latched save still exhausts its extension budget",
          "[zoffset][save_latch]") {
    helix::zoffset::SaveRestartLatch latch;
    latch.on_klippy_ready(false); // restart began and never came back

    REQUIRE(should_extend_save_timeout(latch.restart_latched(), 3, 4));
    // Budget exhausted -> terminal failure even though the latch is set.
    REQUIRE_FALSE(should_extend_save_timeout(latch.restart_latched(), 4, 4));
}
