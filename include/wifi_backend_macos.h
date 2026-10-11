// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#ifdef __APPLE__

#include "lvgl/lvgl.h"
#include "wifi_backend.h"

#include <mutex>

// Use void* for Objective-C objects to avoid mixing C++ and Objective-C in header
// Actual types are cast in the .mm implementation file

/**
 * @brief macOS WiFi backend using CoreWLAN framework
 *
 * Provides real WiFi functionality on macOS using the native CoreWLAN API:
 * - Network scanning via CWInterface scanForNetworksWithSSID
 * - Connection management via CWInterface associateToNetwork
 * - Status queries via CWInterface interfaceState
 * - Event notifications via NSNotificationCenter
 *
 * Architecture:
 * - Uses CoreWLAN framework for all WiFi operations
 * - LVGL timer integration for async event dispatch
 * - Thread-safe event callback system with mutex protection
 * - Automatic permission handling for location services
 *
 * Perfect for:
 * - macOS development with real WiFi testing
 * - Testing UI flows with actual network discovery
 * - Validating connection workflows on real hardware
 */
class WifiBackendMacOS : public WifiBackend {
  public:
    WifiBackendMacOS();
    ~WifiBackendMacOS();

    // ========================================================================
    // WifiBackend Interface Implementation
    // ========================================================================

    WiFiError start() override;
    void stop() override;
    bool is_running() const override;

    WiFiError trigger_scan() override;
    WiFiError get_scan_results(std::vector<WiFiNetwork>& networks) override;
    WiFiError connect_network(const std::string& ssid, const std::string& password,
                              bool is_hidden) override;
    WiFiError disconnect_network() override;
    // Real CoreWLAN radio power. Without these the base-class defaults apply —
    // a silent-success no-op plus an is_radio_enabled() that always answers
    // true — so the UI switch would flip off and immediately snap back on.
    WiFiError set_radio_enabled(bool on) override;
    bool is_radio_enabled() const override;
    ConnectionStatus get_status() override;
    bool supports_5ghz() const override;

  private:
    // ========================================================================
    // Internal State
    // ========================================================================

    bool running_;
    void* wifi_client_; ///< CoreWLAN client (CWWiFiClient*, cast in .mm file)
    void* interface_;   ///< Primary WiFi interface (CWInterface*, cast in .mm file)

    // LVGL timers for async operation completion
    lv_timer_t* scan_timer_;
    lv_timer_t* connect_timer_;

    // Cached scan results (updated after each scan)
    std::vector<WiFiNetwork> cached_networks_;
    std::mutex networks_mutex_; ///< Protects cached_networks_

    // Connection state
    std::string connecting_ssid_;
    std::string connecting_password_;
    bool connection_in_progress_;

    bool supports_5ghz_cached_{true}; ///< Cached 5GHz support (default true for macOS)

    /**
     * @brief Resolve 5GHz support by querying CWInterface supportedWLANChannels
     *
     * Queries the WiFi interface for supported channels and checks for 5GHz band.
     * Sets supports_5ghz_cached_ accordingly. Defaults to true on any error.
     */
    void resolve_5ghz_support();

    // ========================================================================
    // System Validation
    // ========================================================================

    /**
     * @brief Check system prerequisites before starting backend
     *
     * Validates:
     * - CoreWLAN framework availability
     * - WiFi hardware detection
     * - Location services permission (required for scanning on modern macOS)
     *
     * @return WiFiError with detailed status
     */
    WiFiError check_system_prerequisites();

    /**
     * @brief Check if WiFi hardware is available
     *
     * @return WiFiError indicating hardware status
     */
    WiFiError check_wifi_hardware();

    /**
     * @brief Request location services permission if needed
     *
     * macOS 10.15+ requires location permission for WiFi scanning.
     * This method checks current permission state and guides user if denied.
     *
     * @return WiFiError indicating permission status
     */
    WiFiError check_location_permission();

    // ========================================================================
    // Internal Helpers
    // ========================================================================

    /**
     * @brief Extract security type from CoreWLAN network
     *
     * Maps CoreWLAN security types to user-friendly strings:
     * - kCWSecurityNone → "Open"
     * - kCWSecurityWEP → "WEP"
     * - kCWSecurityWPAPersonal → "WPA"
     * - kCWSecurityWPA2Personal → "WPA2"
     * - kCWSecurityWPA3Personal → "WPA3"
     * - etc.
     *
     * @param network CoreWLAN network object (void* to avoid Objective-C in header)
     * @param[out] is_secured Set to true if network requires password
     * @return Security type string
     */
    std::string extract_security_type(void* network, bool& is_secured);

    // ========================================================================
    // LVGL Timer Callbacks (instance methods, called via lambda)
    // ========================================================================

    void scan_timer_callback(lv_timer_t* timer);
    void connect_timer_callback(lv_timer_t* timer);
};

#endif // __APPLE__
