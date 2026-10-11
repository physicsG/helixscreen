// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/**
 * @file wifi_manager_test_access.h
 * @brief The shared friend accessor for driving WiFiManager's private
 *        connection handlers from tests.
 *
 * The only definition: a second one in any test TU is an ODR violation the
 * linker resolves silently. The handlers under test are production's own; all this shim
 * reimplements is the BACKEND-side state write a real nmcli or wpa_supplicant poll would have made
 * before the event fired.
 */

#include "wifi_backend_mock.h"
#include "wifi_manager.h"
#include "wifi_ui_utils.h"

#include <chrono>
#include <functional>
#include <mutex>
#include <string>
#include <utility>

namespace helix {

class WiFiManagerTestAccess {
  public:
    /// Drive handle_connected() with production's ordering: the backend
    /// updates its state BEFORE firing CONNECTED, so is_connected() is
    /// already true when a state observer's refresh runs
    /// (prestonbrown/helixscreen#1059). When the manager's backend is not
    /// the mock, this is the raw handler call.
    static void fire_connected(WiFiManager& wm, const std::string& data = "") {
        if (auto* mock = dynamic_cast<WifiBackendMock*>(wm.backend_.get())) {
            mock->set_connected_state(true, "TestSSID", "192.168.1.100", 75);
        }
        wm.handle_connected(data);
    }

    /// Drive handle_disconnected() with the same ordering: the mock's
    /// disconnect path clears its connected state before firing.
    static void fire_disconnected(WiFiManager& wm, const std::string& data = "") {
        if (auto* mock = dynamic_cast<WifiBackendMock*>(wm.backend_.get())) {
            mock->set_connected_state(false);
        }
        wm.handle_disconnected(data);
    }

    /// Simulate an in-flight connect() without invoking the backend.
    static void begin_connect(WiFiManager& wm, ConnectCallback cb) {
        wm.connect_callback_ = std::move(cb);
        wm.connecting_in_progress_ = true;
    }

    static void fire_auth_failed(WiFiManager& wm, const std::string& data) {
        wm.handle_auth_failed(data);
    }

    /// Drive handle_init_failed() exactly as the backend's INIT_FAILED event
    /// would. The NM->wpa fallback it schedules runs on the next queue drain.
    static void fire_init_failed(WiFiManager& wm, bool silent, const std::string& msg) {
        wm.handle_init_failed(silent, msg);
    }

    /// Whether the scan scheduler would allow another periodic scan — the
    /// latch prestonbrown/helixscreen#1405 is about. False while a scan is
    /// outstanding; permanently false once a backend swap strands one.
    static bool scan_should_trigger(const WiFiManager& wm) {
        return wm.scan_scheduler_.should_trigger();
    }

    /// The backend object the manager is currently driving. Tests read its
    /// dynamic type after an INIT_FAILED to tell a real fallback swap from an
    /// untouched backend — an address comparison would not, since the
    /// replacement is allocated after the old backend is freed and routinely
    /// lands on the same bytes.
    static WifiBackend* backend(const WiFiManager& wm) {
        return wm.backend_.get();
    }

    /// Arm the connect watchdog the way connect() does after the backend
    /// accepts a join, so a test can drive its expiry with lv_tick_inc().
    static void arm_connect_watchdog(WiFiManager& wm) {
        wm.start_connect_timeout();
    }

    static bool grace_pending(WiFiManager& wm) {
        return wm.auth_fail_grace_timer_ != nullptr;
    }

    static bool connecting(WiFiManager& wm) {
        return wm.connecting_in_progress_;
    }

    static void add_observer(WiFiManager& wm, helix::LifetimeToken token,
                             std::function<void()> cb) {
        wm.add_state_observer(std::move(token), std::move(cb));
    }

    static int radio_ops_inflight(WiFiManager& wm) {
        std::lock_guard<std::mutex> lock(wm.radio_op_mutex_);
        return wm.radio_ops_inflight_;
    }

    static void set_sys_root(const std::string& root) {
        WiFiManager::sys_root_ = root;
    }
    static void reset_sys_root() {
        WiFiManager::sys_root_ = "/sys";
    }

    static void set_os_link_probe(std::function<bool()> probe) {
        WiFiManager::os_link_probe_ = std::move(probe);
    }
    static void reset_os_link_probe() {
        WiFiManager::os_link_probe_ = []() {
            return helix::ui::wifi::probe_os_wifi_link().has_link;
        };
    }
    static bool os_link_up() {
        return WiFiManager::os_link_up();
    }
    // Stops the backend directly. set_enabled(false) only disables the radio
    // and keeps the backend alive, so it cannot put trigger_scan() into its
    // NOT_INITIALIZED failure; tests of that path reach in here instead.
    static void stop_backend(WiFiManager& wm) {
        if (wm.backend_) {
            wm.backend_->stop();
        }
    }
    // Backdate the association stamp past ASSOCIATION_GRACE so the expiry side
    // of the suppression is testable without a 5-second sleep.
    static void expire_association_grace(WiFiManager& wm) {
        wm.last_association_change_ = std::chrono::steady_clock::now() -
                                      WiFiManager::ASSOCIATION_GRACE - std::chrono::seconds(1);
    }
    static bool in_association_grace(const WiFiManager& wm) {
        return wm.in_association_grace();
    }
};

} // namespace helix
