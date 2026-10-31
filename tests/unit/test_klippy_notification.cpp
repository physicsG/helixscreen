// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_klippy_notification.cpp
 * @brief What each notify_klippy_* notification means, as both the desktop and
 *        ESP32 Moonraker clients act on it.
 */

#include "klippy_notification.h"

#include "../catch_amalgamated.hpp"

using namespace helix;

TEST_CASE("Klippy notification: disconnected abandons requests and never rediscovers",
          "[klippy][notification]") {
    for (bool completed : {false, true}) {
        const auto n = classify_klippy_notification("notify_klippy_disconnected", completed);
        REQUIRE(n);
        CHECK(n->kind == KlippyNotificationKind::Disconnected);
        CHECK(n->state == KlippyState::SHUTDOWN);
        CHECK(n->abandon_requests);
        CHECK_FALSE(n->rediscover);
        CHECK(n->event.type == MoonrakerEventType::KLIPPY_DISCONNECTED);
        CHECK(n->event.is_error);
    }
}

TEST_CASE("Klippy notification: shutdown rediscovers only when discovery never completed",
          "[klippy][notification]") {
    const auto deferred = classify_klippy_notification("notify_klippy_shutdown", false);
    REQUIRE(deferred);
    CHECK(deferred->kind == KlippyNotificationKind::Shutdown);
    CHECK(deferred->state == KlippyState::SHUTDOWN);
    CHECK_FALSE(deferred->abandon_requests);
    CHECK(deferred->rediscover);
    CHECK(deferred->event.type == MoonrakerEventType::KLIPPY_SHUTDOWN);
    CHECK(deferred->event.is_error);

    const auto completed = classify_klippy_notification("notify_klippy_shutdown", true);
    REQUIRE(completed);
    CHECK_FALSE(completed->rediscover);
}

TEST_CASE("Klippy notification: ready always rediscovers", "[klippy][notification]") {
    for (bool completed : {false, true}) {
        const auto n = classify_klippy_notification("notify_klippy_ready", completed);
        REQUIRE(n);
        CHECK(n->kind == KlippyNotificationKind::Ready);
        CHECK(n->state == KlippyState::READY);
        CHECK_FALSE(n->abandon_requests);
        CHECK(n->rediscover);
        CHECK(n->event.type == MoonrakerEventType::KLIPPY_READY);
        CHECK_FALSE(n->event.is_error);
    }
}

TEST_CASE("Klippy notification: other methods are not klippy notifications",
          "[klippy][notification]") {
    CHECK_FALSE(classify_klippy_notification("notify_status_update", false));
    CHECK_FALSE(classify_klippy_notification("notify_klippy", false));
    CHECK_FALSE(classify_klippy_notification("", true));
}
