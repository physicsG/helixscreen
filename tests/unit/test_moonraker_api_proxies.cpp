// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_moonraker_api_proxies.cpp
 * @brief Unit tests for MoonrakerAPI connection, subscription, and database proxy methods
 *
 * Tests that proxy methods correctly delegate to MoonrakerClient.
 */

#include "ui_update_queue.h"

#include "../../include/moonraker_api.h"
#include "../../include/moonraker_client_mock.h"
#include "../../lvgl/lvgl.h"
#include "../ui_test_utils.h"

#include "../catch_amalgamated.hpp"

using namespace helix;
// ============================================================================
// Global LVGL Initialization
// ============================================================================

namespace {
struct LVGLInitializerProxies {
    LVGLInitializerProxies() {
        static bool initialized = false;
        if (!initialized) {
            lv_init_safe();
            lv_display_t* disp = lv_display_create(800, 480);
            alignas(64) static lv_color_t buf[800 * 10];
            lv_display_set_buffers(disp, buf, NULL, sizeof(buf), LV_DISPLAY_RENDER_MODE_PARTIAL);
            initialized = true;
        }
    }
};

static LVGLInitializerProxies lvgl_init;
} // namespace

// ============================================================================
// Test Fixture
// ============================================================================

class ProxyTestFixture {
  public:
    ProxyTestFixture() : mock_client(MoonrakerClientMock::PrinterType::VORON_24) {
        state.init_subjects(false);
        mock_client.connect("ws://mock:7125/websocket", []() {}, []() {});
        api = std::make_unique<MoonrakerAPI>(mock_client, state);
        mock_client.discover_printer([]() {});
    }

    ~ProxyTestFixture() {
        // Drain while `state` is still alive. discover_printer() in the ctor
        // routes hardware results into PrinterCapabilitiesState's deferred
        // setters; without this they sit in the queue for whichever test drains
        // next, which is what the cross-test leak report keys on (#1166).
        helix::ui::UpdateQueue::instance().drain();

        mock_client.stop_temperature_simulation();
        mock_client.disconnect();
        api.reset();
    }

    MoonrakerClientMock mock_client;
    PrinterState state;
    std::unique_ptr<MoonrakerAPI> api;
};

// ============================================================================
// Connection State Proxy Tests
// ============================================================================

TEST_CASE_METHOD(ProxyTestFixture, "is_connected returns true when client is connected",
                 "[api][proxy]") {
    REQUIRE(api->is_connected());
}

TEST_CASE_METHOD(ProxyTestFixture, "is_connected returns false after disconnect", "[api][proxy]") {
    mock_client.disconnect();
    REQUIRE_FALSE(api->is_connected());
}

TEST_CASE_METHOD(ProxyTestFixture, "get_connection_state mirrors client state", "[api][proxy]") {
    REQUIRE(api->get_connection_state() == ConnectionState::CONNECTED);
    mock_client.disconnect();
    REQUIRE(api->get_connection_state() == ConnectionState::DISCONNECTED);
}

TEST_CASE_METHOD(ProxyTestFixture, "get_websocket_url returns client URL", "[api][proxy]") {
    // Mock client's connect() doesn't store last_url_ (private to base class),
    // so this returns empty string. Verify the proxy delegates without crashing.
    std::string url = api->get_websocket_url();
    // With real client this would be non-empty; mock doesn't populate it
    REQUIRE(url == mock_client.get_last_url());
}

// ============================================================================
// Subscription Proxy Tests
// ============================================================================

TEST_CASE_METHOD(ProxyTestFixture, "subscribe_notifications returns valid ID", "[api][proxy]") {
    SubscriptionId id = api->subscribe_notifications([](json) {});
    REQUIRE(id != INVALID_SUBSCRIPTION_ID);
}

TEST_CASE_METHOD(ProxyTestFixture, "unsubscribe_notifications returns true for valid ID",
                 "[api][proxy]") {
    SubscriptionId id = api->subscribe_notifications([](json) {});
    REQUIRE(api->unsubscribe_notifications(id));
}

TEST_CASE_METHOD(ProxyTestFixture, "unsubscribe_notifications returns false for invalid ID",
                 "[api][proxy]") {
    REQUIRE_FALSE(api->unsubscribe_notifications(999999));
}

TEST_CASE_METHOD(ProxyTestFixture, "subscribe/unsubscribe roundtrip works", "[api][proxy]") {
    // Subscribe multiple callbacks
    SubscriptionId id1 = api->subscribe_notifications([](json) {});
    SubscriptionId id2 = api->subscribe_notifications([](json) {});

    REQUIRE(id1 != id2);
    REQUIRE(id1 != INVALID_SUBSCRIPTION_ID);
    REQUIRE(id2 != INVALID_SUBSCRIPTION_ID);

    // Unsubscribe both
    REQUIRE(api->unsubscribe_notifications(id1));
    REQUIRE(api->unsubscribe_notifications(id2));

    // Double unsubscribe should fail
    REQUIRE_FALSE(api->unsubscribe_notifications(id1));
}

// ============================================================================
// Method Callback Proxy Tests
// ============================================================================

TEST_CASE_METHOD(ProxyTestFixture, "register/unregister method callback", "[api][proxy]") {
    // Register should not throw
    api->register_method_callback("notify_gcode_response", "test_handler", [](json) {});

    // Unregister should succeed
    REQUIRE(api->unregister_method_callback("notify_gcode_response", "test_handler"));

    // Double unregister should fail
    REQUIRE_FALSE(api->unregister_method_callback("notify_gcode_response", "test_handler"));
}

TEST_CASE_METHOD(ProxyTestFixture, "unregister nonexistent method callback returns false",
                 "[api][proxy]") {
    REQUIRE_FALSE(api->unregister_method_callback("nonexistent_method", "no_handler"));
}

// ============================================================================
// Disconnect Modal Suppression Proxy Tests
// ============================================================================

TEST_CASE_METHOD(ProxyTestFixture, "suppress_disconnect_modal forwards to client", "[api][proxy]") {
    // Should not throw, and client should report suppressed
    api->suppress_disconnect_modal(5000);
    REQUIRE(mock_client.is_disconnect_modal_suppressed());
}

// ============================================================================
// Database Operation Proxy Tests
// ============================================================================

// The mock has no server.database.* handler, so its send_jsonrpc records the call
// and returns without ever invoking a callback. That is enough to pin the wire
// format (last_send_method()), but it means the response lambdas inside
// MoonrakerAPI never run against the mock. The null-callback cases below therefore
// use a real, unconnected MoonrakerClient, whose pre-send guard invokes the error
// callback synchronously (moonraker_client.cpp ready_to_send) and so actually
// executes the `if (on_error)` branch.

TEST_CASE_METHOD(ProxyTestFixture, "database_get_item sends correct JSON-RPC", "[api][proxy]") {
    api->database_get_item("helix", "settings", [](const json&) {}, [](const MoonrakerError&) {});

    REQUIRE(mock_client.last_send_method() == "server.database.get_item");
}

TEST_CASE_METHOD(ProxyTestFixture, "database_post_item sends correct JSON-RPC", "[api][proxy]") {
    json value = {{"theme", "dark"}, {"language", "en"}};

    api->database_post_item("helix", "settings", value, []() {}, [](const MoonrakerError&) {});

    REQUIRE(mock_client.last_send_method() == "server.database.post_item");
}

TEST_CASE_METHOD(ProxyTestFixture, "database_get_item error path tolerates a null error callback",
                 "[api][proxy]") {
    MoonrakerClient offline;
    MoonrakerAPI offline_api(offline, state);

    // Paired non-null run first: proves the error lambda really is reached in this
    // setup, so the null run below is exercising the guard rather than nothing.
    bool success_invoked = false;
    bool error_invoked = false;
    offline_api.database_get_item(
        "helix", "key", [&success_invoked](const json&) { success_invoked = true; },
        [&error_invoked](const MoonrakerError&) { error_invoked = true; });
    REQUIRE(error_invoked);
    REQUIRE_FALSE(success_invoked);

    // Same path, null error callback: the `if (on_error)` guard now executes.
    REQUIRE_NOTHROW(offline_api.database_get_item("helix", "key", [](const json&) {}, nullptr));
}

TEST_CASE_METHOD(ProxyTestFixture, "database_post_item error path tolerates null callbacks",
                 "[api][proxy]") {
    MoonrakerClient offline;
    MoonrakerAPI offline_api(offline, state);

    bool success_invoked = false;
    bool error_invoked = false;
    offline_api.database_post_item(
        "helix", "key", json{{"val", 1}}, [&success_invoked]() { success_invoked = true; },
        [&error_invoked](const MoonrakerError&) { error_invoked = true; });
    REQUIRE(error_invoked);
    REQUIRE_FALSE(success_invoked);

    REQUIRE_NOTHROW(
        offline_api.database_post_item("helix", "key", json{{"val", 1}}, nullptr, nullptr));
}
