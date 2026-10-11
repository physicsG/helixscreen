// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ui_overlay_network_settings.h"

#include "wifi_backend.h"

#include <cstring>
#include <string>
#include <vector>

/**
 * @brief Reaches the overlay's cached modal pointers.
 *
 * These are the handles the async connect callbacks null-check and then walk
 * (#1341). A test needs to see them directly, because "the handle is null once
 * its modal dies" is the whole invariant; a dangling pointer would be
 * indistinguishable from a live one at the call site.
 */
class NetworkSettingsOverlayTestAccess {
  public:
    static helix::ui::WidgetRef& password_modal(NetworkSettingsOverlay& o) {
        return o.password_modal_;
    }
    static helix::ui::WidgetRef& hidden_network_modal(NetworkSettingsOverlay& o) {
        return o.hidden_network_modal_;
    }
    static helix::ui::WidgetRef& test_modal(NetworkSettingsOverlay& o) {
        return o.test_modal_;
    }
    static helix::ui::WidgetRef& step_widget(NetworkSettingsOverlay& o) {
        return o.step_widget_;
    }
    /// The overlay's manager pointer — tests point it at a locally owned
    /// WiFiManager instead of the process-global singleton, before create().
    static std::shared_ptr<helix::WiFiManager>& wifi_manager(NetworkSettingsOverlay& o) {
        return o.wifi_manager_;
    }
    /// The wifi_connected subject (0/1) — the observable a transport refresh
    /// must move.
    static lv_subject_t& wifi_connected(NetworkSettingsOverlay& o) {
        return o.wifi_connected_;
    }
    /// The SSID the password modal is about. The handlers read it rather than
    /// taking it as an argument, so a test has to seed it.
    static void set_current_ssid(NetworkSettingsOverlay& o, const std::string& ssid) {
        strncpy(o.current_ssid_, ssid.c_str(), sizeof(o.current_ssid_) - 1);
        o.current_ssid_[sizeof(o.current_ssid_) - 1] = '\0';
    }
    /// The production handler behind the password modal's Connect button.
    static void password_connect_clicked(NetworkSettingsOverlay& o) {
        o.handle_password_connect_clicked();
    }
    /// Open the hidden-network modal, exactly as the "Add other network"
    /// button does.
    static void add_other_clicked(NetworkSettingsOverlay& o) {
        o.handle_add_other_clicked();
    }
    /// The production handler behind the hidden-network modal's Connect button.
    static void hidden_connect_clicked(NetworkSettingsOverlay& o) {
        o.handle_hidden_connect_clicked();
    }
    /// Build the scan rows synchronously, with no network connected — the click
    /// handler reads per-row data only this path attaches.
    static void populate(NetworkSettingsOverlay& o, const std::vector<WiFiNetwork>& networks) {
        o.build_network_list(networks, WifiBackend::ConnectionStatus{});
    }
    /// What a completed scan does: the rows are built once the connection
    /// read lands.
    static void scan_completed(NetworkSettingsOverlay& o,
                               const std::vector<WiFiNetwork>& networks) {
        o.populate_network_list(networks);
    }
    static void update_wifi_status(NetworkSettingsOverlay& o) {
        o.update_wifi_status();
    }
    static void forget_clicked(NetworkSettingsOverlay& o) {
        o.handle_network_settings_forget();
    }
    static const std::string& pending_forget_ssid(NetworkSettingsOverlay& o) {
        return o.pending_forget_ssid_;
    }
    static std::string ssid(NetworkSettingsOverlay& o) {
        return o.ssid_buffer_;
    }
    static std::string ip(NetworkSettingsOverlay& o) {
        return o.ip_buffer_;
    }
    static std::string mac(NetworkSettingsOverlay& o) {
        return o.mac_buffer_;
    }
    static lv_obj_t* networks_list(NetworkSettingsOverlay& o) {
        return o.networks_list_;
    }
};
