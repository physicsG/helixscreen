// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#include "action_prompt_manager.h" // helix::PromptData / helix::PromptButton
#include "error_event.h"
#include "gcode_error_router.h"

#include "catch_amalgamated.hpp"

using helix::ErrorEvent;
using helix::ErrorSeverity;

TEST_CASE("CRITICAL with no actions -> MODAL", "[error-center][routing]") {
    ErrorEvent e;
    e.severity = ErrorSeverity::CRITICAL;
    REQUIRE(helix::decide_presentation(e) == helix::PresentAs::MODAL);
}
TEST_CASE("CRITICAL with a recovery action -> MODAL_WITH_RECOVER", "[error-center][routing]") {
    ErrorEvent e;
    e.severity = ErrorSeverity::CRITICAL;
    e.recovery_actions.push_back({"Reset CFS", "BOX_ERROR_CLEAR", "t"});
    REQUIRE(helix::decide_presentation(e) == helix::PresentAs::MODAL_WITH_RECOVER);
}
TEST_CASE("WARNING with recover action -> TOAST_WITH_RECOVER", "[error-center][routing]") {
    ErrorEvent e;
    e.severity = ErrorSeverity::WARNING;
    e.recovery_actions.push_back({"Recover", "", "t"});
    REQUIRE(helix::decide_presentation(e) == helix::PresentAs::TOAST_WITH_RECOVER);
}
TEST_CASE("WARNING no actions -> TOAST", "[error-center][routing]") {
    ErrorEvent e;
    e.severity = ErrorSeverity::WARNING;
    REQUIRE(helix::decide_presentation(e) == helix::PresentAs::TOAST);
}
TEST_CASE("INFO -> NONE", "[error-center][routing]") {
    ErrorEvent e;
    e.severity = ErrorSeverity::INFO;
    REQUIRE(helix::decide_presentation(e) == helix::PresentAs::NONE);
}

TEST_CASE("build_recovery_prompt maps actions to buttons", "[error-center][routing]") {
    ErrorEvent e;
    e.severity = ErrorSeverity::CRITICAL;
    e.title = "Toolhead jam";
    e.detail = "Possible filament break at the toolhead.";
    e.recovery_actions.push_back({"Resume", "RESUME", "afc::resume", "primary"});
    e.recovery_actions.push_back({"Unload", "TOOL_UNLOAD", "afc::tool_unload", ""});
    e.recovery_actions.push_back({"Recover", "AFC_RESET", "afc::reset", "danger"});

    helix::PromptData p = helix::build_recovery_prompt(e);

    REQUIRE(p.title == "Toolhead jam");
    REQUIRE(p.text_lines.size() == 1);
    REQUIRE(p.text_lines[0] == "Possible filament break at the toolhead.");
    REQUIRE(p.buttons.size() == 3);
    REQUIRE(p.buttons[0].label == "Resume");
    REQUIRE(p.buttons[0].gcode == "RESUME");
    REQUIRE(p.buttons[0].color == "primary");
    REQUIRE(p.buttons[1].label == "Unload");
    REQUIRE(p.buttons[1].color.empty());    // neutral
    REQUIRE(p.buttons[2].color == "error"); // "danger" -> "error"
}

TEST_CASE("build_recovery_prompt falls back to default title", "[error-center][routing]") {
    ErrorEvent e;
    e.severity = ErrorSeverity::CRITICAL;
    e.detail = "x";
    e.recovery_actions.push_back({"Reset CFS", "BOX_ERROR_CLEAR", "t", ""});
    helix::PromptData p = helix::build_recovery_prompt(e);
    REQUIRE_FALSE(p.title.empty()); // non-empty default title
    REQUIRE(p.buttons.size() == 1);
}

// --- recover-toast dispatch (#1172) -------------------------------------
//
// present_recover_toast() was hardwired to the key298 PrinterRecoveryService
// and never read ErrorEvent::recovery_actions, so any classifier producing a
// WARNING with its own action silently got offered a klipper_mcu bounce for an
// unrelated fault. That constrained two designs into raising their severity to
// CRITICAL purely to route around this path (#1152, #1182). These pin the
// dispatch decision that now drives it.

using helix::RecoverDispatch;

namespace {
ErrorEvent warning_with(std::vector<helix::RecoveryAction> actions) {
    ErrorEvent e;
    e.severity = ErrorSeverity::WARNING;
    e.detail = "something went wrong";
    e.recovery_actions = std::move(actions);
    return e;
}
} // namespace

TEST_CASE("recover toast runs the event's own gcode", "[error-center][routing][1172]") {
    // The whole point: a WARNING carrying its own action gets THAT action,
    // not the key298 recovery service.
    ErrorEvent e = warning_with({{"Re-seat lane", "AFC_RESEAT LANE=lane1", "afc::reseat", ""}});
    REQUIRE(helix::decide_presentation(e) == helix::PresentAs::TOAST_WITH_RECOVER);
    REQUIRE(helix::decide_recover_dispatch(e) == RecoverDispatch::GCODE);
}

TEST_CASE("key298 still routes to the recovery service", "[error-center][routing][1172]") {
    // Identified by its log_tag, not by its empty gcode — an empty gcode now
    // legitimately means "dismiss", so matching on that would misroute.
    ErrorEvent e = warning_with({{"Recover", "", "error_classify::key298_recover", ""}});
    REQUIRE(helix::decide_recover_dispatch(e) == RecoverDispatch::RECOVERY_SERVICE);
}

TEST_CASE("several actions escalate to the modal", "[error-center][routing][1172]") {
    // A toast has one button. Rendering only the first would silently discard
    // the user's other way out.
    ErrorEvent e = warning_with(
        {{"Resume", "RESUME", "a::resume", ""}, {"Unload", "TOOL_UNLOAD", "a::unload", ""}});
    REQUIRE(helix::decide_recover_dispatch(e) == RecoverDispatch::ESCALATE_TO_MODAL);
}

TEST_CASE("a lone action needing a hot nozzle escalates to the modal",
          "[error-center][routing][1172]") {
    // A toast has no preheat gate; firing cold fails exactly the way the
    // operation that raised the error did (#1193). Only the modal presenter
    // knows how to preheat and defer — so this escalates despite being the
    // single-action shape a toast could otherwise render.
    helix::RecoveryAction hot{"Resume", "RESUME", "a::resume", ""};
    hot.needs_hot_nozzle = true;
    ErrorEvent e = warning_with({hot});
    REQUIRE(e.recovery_actions.size() == 1);
    REQUIRE(helix::decide_recover_dispatch(e) == RecoverDispatch::ESCALATE_TO_MODAL);
}

TEST_CASE("an unrunnable action falls back to a plain toast", "[error-center][routing][1172]") {
    // Neither a gcode nor the key298 service: there is nothing to run, so a
    // button would be a tap that silently does nothing. The fault must still
    // be surfaced, hence PLAIN_TOAST rather than showing nothing at all.
    ErrorEvent e = warning_with({{"OK", "", "some::dismiss", ""}});
    REQUIRE(helix::decide_recover_dispatch(e) == RecoverDispatch::PLAIN_TOAST);
}

TEST_CASE("no actions at all is a plain toast, not a crash", "[error-center][routing][1172]") {
    // decide_presentation() should never route an empty vector here, but
    // front() on it would be UB if it ever did.
    ErrorEvent e = warning_with({});
    REQUIRE(helix::decide_recover_dispatch(e) == RecoverDispatch::PLAIN_TOAST);
}
