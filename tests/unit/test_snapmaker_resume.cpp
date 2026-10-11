// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../include/pause_cause.h"
#include "../../include/snapmaker_resume.h"

#include "../catch_amalgamated.hpp"

using helix::classify_pause;
using helix::PauseCause;
using helix::PauseSignals;
using helix::snapmaker_terminal_matchers;

TEST_CASE("snapmaker_terminal_matchers: dirty-bed by id 532 => Terminal", "[pause][snapmaker]") {
    PauseSignals s;
    s.exception_id = 532;
    s.exception_code = 1;
    s.message = "detected dirty bed";
    REQUIRE(classify_pause(s, snapmaker_terminal_matchers()) == PauseCause::Terminal);
}

// Exception 532 is the defect_detection family: code 1 dirty bed, 2 noodle,
// 3 residue, 4 dirty nozzle. The firmware PAUSEs for all four, so only the
// hardware-verified dirty-bed code (#991) is Terminal — the rest attempt RESUME.
TEST_CASE("snapmaker_terminal_matchers: id 532 code 2 (noodle) => Recoverable",
          "[pause][snapmaker]") {
    PauseSignals s;
    s.exception_id = 532;
    s.exception_code = 2;
    s.message = "detected noodle";
    s.sdcard_active = false; // firmware defect pauses deactivate the SD
    REQUIRE(classify_pause(s, snapmaker_terminal_matchers()) == PauseCause::Recoverable);
}

TEST_CASE("snapmaker_terminal_matchers: id 532 code 3 (residue) => Recoverable",
          "[pause][snapmaker]") {
    PauseSignals s;
    s.exception_id = 532;
    s.exception_code = 3;
    s.message = "detected residue";
    s.sdcard_active = false;
    REQUIRE(classify_pause(s, snapmaker_terminal_matchers()) == PauseCause::Recoverable);
}

TEST_CASE("snapmaker_terminal_matchers: id 532 code 4 (dirty nozzle) => Recoverable",
          "[pause][snapmaker]") {
    PauseSignals s;
    s.exception_id = 532;
    s.exception_code = 4;
    s.message = "detected dirty nozzle";
    s.sdcard_active = false;
    REQUIRE(classify_pause(s, snapmaker_terminal_matchers()) == PauseCause::Recoverable);
}

TEST_CASE("snapmaker_terminal_matchers: dirty-bed by message => Terminal", "[pause][snapmaker]") {
    PauseSignals s;
    s.exception_id = -1; // id missing, only message present
    s.message = "detected dirty bed";
    REQUIRE(classify_pause(s, snapmaker_terminal_matchers()) == PauseCause::Terminal);
}

TEST_CASE("snapmaker_terminal_matchers: runout (id 523) => Recoverable", "[pause][snapmaker]") {
    // Runout also deactivates virtual_sdcard, so the matchers must NOT key on
    // sdcard state — id 523 / "runout" must classify Recoverable.
    PauseSignals s;
    s.exception_id = 523;
    s.message = "e1_filament runout";
    s.sdcard_active = false;
    s.runout_tripped = true;
    REQUIRE(classify_pause(s, snapmaker_terminal_matchers()) == PauseCause::Recoverable);
}

TEST_CASE("snapmaker_terminal_matchers: user pause (empty signals) => Unknown",
          "[pause][snapmaker]") {
    PauseSignals s; // empty message, exception -1, sdcard active, no runout
    REQUIRE(classify_pause(s, snapmaker_terminal_matchers()) == PauseCause::Unknown);
}
