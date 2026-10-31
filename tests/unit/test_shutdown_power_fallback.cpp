// Copyright (C) 2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../../src/ui/panel_widgets/shutdown_widget.h"
#include "../helix_test_fixture.h"

#include <string>

#include "../catch_amalgamated.hpp"

/**
 * Local-fallback policy for a failed Moonraker machine.reboot / machine.shutdown.
 *
 * Non-systemd printer hosts run Moonraker with `provider: none`, whose
 * BaseProvider implements reboot/shutdown as `sudo systemctl ...`. Hosts like
 * the Creality K2 (OpenWrt/procd) ship neither `sudo` nor `systemctl`, so those
 * RPCs can only ever fail — the user gets "Reboot failed" and no way to power
 * the machine down from the UI, even though busybox /sbin/reboot is right there
 * and SystemPower already knows how to call it.
 *
 * When helixscreen runs ON the printer host, "reboot the printer" and "reboot
 * this device" are the same act, so a failed RPC may be retried locally. When
 * the printer is a *different* host, it must not be: rebooting locally would
 * take down the screen while the printer stays up.
 */
class ShutdownFallbackFixture : public HelixTestFixture {};

TEST_CASE_METHOD(ShutdownFallbackFixture, "Same-host machine action failure falls back to local",
                 "[shutdown][power]") {
    SECTION("reboot failure runs the local action") {
        int calls = 0;
        const helix::PowerResult handled = helix::handle_machine_power_failure(
            "sudo: not found", /*is_reboot=*/true, /*allow_local_fallback=*/true, [&calls]() {
                ++calls;
                return helix::PowerResult::Dispatched;
            });

        CHECK(calls == 1);
        CHECK(handled == helix::PowerResult::Dispatched);
    }

    SECTION("shutdown failure runs the local action") {
        int calls = 0;
        const helix::PowerResult handled = helix::handle_machine_power_failure(
            "sudo: not found", /*is_reboot=*/false, /*allow_local_fallback=*/true, [&calls]() {
                ++calls;
                return helix::PowerResult::Dispatched;
            });

        CHECK(calls == 1);
        CHECK(handled == helix::PowerResult::Dispatched);
    }

    SECTION("a local action that itself fails reports failure") {
        int calls = 0;
        const helix::PowerResult handled = helix::handle_machine_power_failure(
            "sudo: not found", /*is_reboot=*/true, /*allow_local_fallback=*/true, [&calls]() {
                ++calls;
                return helix::PowerResult::Failed;
            });

        CHECK(calls == 1);
        CHECK(handled == helix::PowerResult::Failed);
    }
}

TEST_CASE_METHOD(ShutdownFallbackFixture, "Remote-printer machine action failure stays remote",
                 "[shutdown][power]") {
    // Dual-scope "Printer" target: the failing host is NOT this one. Powering
    // this device down instead would kill the screen and leave the printer up.
    int calls = 0;
    const helix::PowerResult handled = helix::handle_machine_power_failure(
        "Klippy Request Timed Out", /*is_reboot=*/true, /*allow_local_fallback=*/false, [&calls]() {
            ++calls;
            return helix::PowerResult::Dispatched;
        });

    CHECK(calls == 0);
    CHECK(handled == helix::PowerResult::Failed);
}

TEST_CASE_METHOD(ShutdownFallbackFixture, "Missing local action is not invoked",
                 "[shutdown][power]") {
    // Defensive: an empty std::function must not be called even when the
    // fallback is permitted.
    CHECK(helix::handle_machine_power_failure("boom", /*is_reboot=*/true,
                                              /*allow_local_fallback=*/true,
                                              {}) == helix::PowerResult::Failed);
}

TEST_CASE_METHOD(ShutdownFallbackFixture,
                 "Same-host fallback refused by polkit reports the missing permission",
                 "[shutdown][power]") {
    int calls = 0;
    const helix::PowerResult handled =
        helix::handle_machine_power_failure("sudo: a password is required", /*is_reboot=*/false,
                                            /*allow_local_fallback=*/true, [&calls]() {
                                                ++calls;
                                                return helix::PowerResult::NotAuthorized;
                                            });

    CHECK(calls == 1);
    CHECK(handled == helix::PowerResult::NotAuthorized);
}

TEST_CASE("Machine power failure toast text", "[shutdown][power]") {
    using helix::PowerResult;
    CHECK(helix::machine_power_failure_message(PowerResult::Dispatched, false) == nullptr);
    CHECK(std::string(helix::machine_power_failure_message(PowerResult::Failed, false)) ==
          "Shutdown failed");
    CHECK(std::string(helix::machine_power_failure_message(PowerResult::Failed, true)) ==
          "Reboot failed");
    // A refusal says what fixes it, in the screen path's words
    for (const bool is_reboot : {false, true}) {
        CHECK(std::string(
                  helix::machine_power_failure_message(PowerResult::NotAuthorized, is_reboot)) ==
              helix::screen_power_failure_message(PowerResult::NotAuthorized, is_reboot));
        CHECK(
            std::string(helix::machine_power_failure_message(PowerResult::NotAuthorized, is_reboot))
                .find("not permitted") != std::string::npos);
    }
}
