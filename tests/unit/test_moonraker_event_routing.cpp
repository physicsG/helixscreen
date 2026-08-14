// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

// Routing decisions for Moonraker events (#1219). The decision is pure — no
// LVGL, no clock, no globals — which is the whole point: it used to live inline
// in a lambda that ran on the libhv event-loop thread and called lv_tr() there.
// Pulling it out is what lets the caller apply lv_tr() on the main thread.
//
// These cases pin the routing table, including the two orderings that matter:
// recovery events must NOT be suppressible by the wizard or the grace period,
// and deferred-discovery must be suppressed before the connection-failed check.

#include "moonraker_event_routing.h"

#include <string>

#include "../catch_amalgamated.hpp"

using helix::decide_moonraker_event;
using helix::MoonrakerEventRoute;
using helix::MoonrakerEventSuppression;

namespace {
constexpr bool IS_ERROR = true;
constexpr bool NOT_ERROR = false;
constexpr bool IN_GRACE = true;
constexpr bool AFTER_GRACE = false;
constexpr bool WIZARD_UP = true;
constexpr bool NO_WIZARD = false;
constexpr bool MODAL_UP = true;
constexpr bool NO_MODAL = false;
} // namespace

TEST_CASE("Recovery events route to the unified dialog", "[moonraker][routing][1219]") {
    auto d = decide_moonraker_event(MoonrakerEventType::KLIPPY_DISCONNECTED, IS_ERROR, AFTER_GRACE,
                                    NO_WIZARD);
    REQUIRE(d.route == MoonrakerEventRoute::RecoveryDisconnected);

    d = decide_moonraker_event(MoonrakerEventType::KLIPPY_SHUTDOWN, IS_ERROR, AFTER_GRACE,
                               NO_WIZARD);
    REQUIRE(d.route == MoonrakerEventRoute::RecoveryShutdown);
}

TEST_CASE("Recovery events survive the wizard and the startup grace period",
          "[moonraker][routing][1219]") {
    // A disconnected or shut-down Klippy is not startup noise. If either
    // suppression check were hoisted above the recovery branch, the dialog would
    // silently not appear during the first 30 s or behind the setup wizard.
    for (auto type :
         {MoonrakerEventType::KLIPPY_DISCONNECTED, MoonrakerEventType::KLIPPY_SHUTDOWN}) {
        for (bool err : {IS_ERROR, NOT_ERROR}) {
            auto d = decide_moonraker_event(type, err, IN_GRACE, WIZARD_UP);
            INFO("type=" << static_cast<int>(type) << " is_error=" << err);
            REQUIRE(d.route != MoonrakerEventRoute::Ignore);
        }
    }
}

TEST_CASE("Connection failure gets the Change-Address prompt, not a toast",
          "[moonraker][routing][1219]") {
    auto d = decide_moonraker_event(MoonrakerEventType::CONNECTION_FAILED, IS_ERROR, AFTER_GRACE,
                                    NO_WIZARD);
    REQUIRE(d.route == MoonrakerEventRoute::ConnectionFailedModal);
    REQUIRE(std::string(d.title_tag) == "Connection Failed");
}

TEST_CASE("Connection failure degrades to a toast while a modal is open", "[moonraker][routing]") {
    // AD5X bundle 865DXBQ7: the latched CONNECTION_FAILED fires ~60 s after
    // startup, which on an unreachable printer is exactly when the user is in
    // Settings > Network typing a WiFi password to fix it. The prompt was pushed
    // at modal stack depth 2, over that keyboard, and the password had to be
    // retyped from scratch. A toast carries the same information without taking
    // the screen away.
    auto d = decide_moonraker_event(MoonrakerEventType::CONNECTION_FAILED, IS_ERROR, AFTER_GRACE,
                                    NO_WIZARD, MODAL_UP);
    REQUIRE(d.route == MoonrakerEventRoute::ErrorToast);
    REQUIRE(std::string(d.title_tag) == "Connection Failed");

    SECTION("and still gets the full prompt when nothing is open") {
        auto clear = decide_moonraker_event(MoonrakerEventType::CONNECTION_FAILED, IS_ERROR,
                                            AFTER_GRACE, NO_WIZARD, NO_MODAL);
        REQUIRE(clear.route == MoonrakerEventRoute::ConnectionFailedModal);
    }

    SECTION("an open modal does not suppress the event entirely") {
        // Degrade, never drop: the connection state has to reach the user
        // somehow, and this event fires once per session.
        REQUIRE(d.route != MoonrakerEventRoute::Ignore);
    }

    SECTION("an open modal does not reroute the recovery dialogs") {
        // Those are not "notifications" — a shut-down Klippy needs its dialog
        // whatever else is on screen.
        REQUIRE(decide_moonraker_event(MoonrakerEventType::KLIPPY_SHUTDOWN, IS_ERROR, AFTER_GRACE,
                                       NO_WIZARD, MODAL_UP)
                    .route == MoonrakerEventRoute::RecoveryShutdown);
        REQUIRE(decide_moonraker_event(MoonrakerEventType::KLIPPY_DISCONNECTED, IS_ERROR,
                                       AFTER_GRACE, NO_WIZARD, MODAL_UP)
                    .route == MoonrakerEventRoute::RecoveryDisconnected);
    }
}

TEST_CASE("Deferred discovery is suppressed before the error routing",
          "[moonraker][routing][1219]") {
    auto d = decide_moonraker_event(MoonrakerEventType::DISCOVERY_DEFERRED, IS_ERROR, AFTER_GRACE,
                                    NO_WIZARD);
    REQUIRE(d.route == MoonrakerEventRoute::Ignore);
    REQUIRE(d.suppressed_because == MoonrakerEventSuppression::DiscoveryDeferred);
}

TEST_CASE("Error events carry the right untranslated title tag", "[moonraker][routing][1219]") {
    auto rpc =
        decide_moonraker_event(MoonrakerEventType::RPC_ERROR, IS_ERROR, AFTER_GRACE, NO_WIZARD);
    REQUIRE(rpc.route == MoonrakerEventRoute::ErrorToast);
    REQUIRE(std::string(rpc.title_tag) == "Request Failed");

    // Anything else that is an error falls back to the generic title.
    auto other =
        decide_moonraker_event(MoonrakerEventType::KLIPPY_READY, IS_ERROR, AFTER_GRACE, NO_WIZARD);
    REQUIRE(other.route == MoonrakerEventRoute::ErrorToast);
    REQUIRE(std::string(other.title_tag) == "Printer Error");
}

TEST_CASE("Title tags are returned untranslated", "[moonraker][routing][1219]") {
    // The regression this guards: if the decision ever calls lv_tr() itself, it
    // is back to translating on whatever thread raised the event. Source strings
    // are the English tags verbatim, and the routing TU must not link LVGL at all.
    REQUIRE(std::string(decide_moonraker_event(MoonrakerEventType::CONNECTION_FAILED, IS_ERROR,
                                               AFTER_GRACE, NO_WIZARD)
                            .title_tag) == "Connection Failed");
    REQUIRE(std::string(decide_moonraker_event(MoonrakerEventType::RPC_ERROR, IS_ERROR, AFTER_GRACE,
                                               NO_WIZARD)
                            .title_tag) == "Request Failed");
}

TEST_CASE("Non-error toasts are suppressed during the wizard", "[moonraker][routing][1219]") {
    auto d =
        decide_moonraker_event(MoonrakerEventType::KLIPPY_READY, NOT_ERROR, AFTER_GRACE, WIZARD_UP);
    REQUIRE(d.route == MoonrakerEventRoute::Ignore);
    REQUIRE(d.suppressed_because == MoonrakerEventSuppression::Wizard);
}

TEST_CASE("Klipper-ready is suppressed only inside the grace period",
          "[moonraker][routing][1219]") {
    auto inside =
        decide_moonraker_event(MoonrakerEventType::KLIPPY_READY, NOT_ERROR, IN_GRACE, NO_WIZARD);
    REQUIRE(inside.route == MoonrakerEventRoute::Ignore);
    REQUIRE(inside.suppressed_because == MoonrakerEventSuppression::StartupGrace);

    // Both sides of the branch: a later ready event is a real reconnection.
    auto outside =
        decide_moonraker_event(MoonrakerEventType::KLIPPY_READY, NOT_ERROR, AFTER_GRACE, NO_WIZARD);
    REQUIRE(outside.route == MoonrakerEventRoute::WarningToast);
}

TEST_CASE("The grace period does not suppress non-ready warnings", "[moonraker][routing][1219]") {
    // Only KLIPPY_READY is startup noise. A different warning arriving in the
    // first 30 s is still worth showing.
    auto d = decide_moonraker_event(MoonrakerEventType::RPC_ERROR, NOT_ERROR, IN_GRACE, NO_WIZARD);
    REQUIRE(d.route == MoonrakerEventRoute::WarningToast);
}

TEST_CASE("Routes that need no title report none", "[moonraker][routing][1219]") {
    // Guards against a caller passing nullptr into lv_tr(): every route that
    // yields a title must have one, and the rest must be explicit about not.
    struct Case {
        MoonrakerEventType type;
        bool is_error;
    };
    const Case cases[] = {
        {MoonrakerEventType::KLIPPY_DISCONNECTED, IS_ERROR},
        {MoonrakerEventType::KLIPPY_SHUTDOWN, IS_ERROR},
        {MoonrakerEventType::DISCOVERY_DEFERRED, IS_ERROR},
        {MoonrakerEventType::CONNECTION_FAILED, IS_ERROR},
        {MoonrakerEventType::RPC_ERROR, IS_ERROR},
        {MoonrakerEventType::KLIPPY_READY, NOT_ERROR},
    };
    for (const auto& c : cases) {
        auto d = decide_moonraker_event(c.type, c.is_error, AFTER_GRACE, NO_WIZARD);
        INFO("type=" << static_cast<int>(c.type));
        const bool needs_title = d.route == MoonrakerEventRoute::ErrorToast ||
                                 d.route == MoonrakerEventRoute::ConnectionFailedModal;
        REQUIRE(needs_title == (d.title_tag != nullptr));
    }
}
