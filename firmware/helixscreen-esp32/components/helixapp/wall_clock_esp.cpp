// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "wall_clock_esp.h"

#include "esp_http_lane.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "format_utils.h"

#include <atomic>
#include <ctime>
#include <sys/time.h>

namespace helix::wall_clock_esp {

namespace {

constexpr char TAG[] = "wall_clock";

std::atomic<bool> s_set{false};
std::atomic<bool> s_sntp_synced{false};

// lwIP's thread. SNTP has already set the clock; this only stops a later Date
// header from overriding it.
void on_sntp_sync(timeval* tv) {
    s_sntp_synced.store(true);
    if (!s_set.exchange(true)) {
        ESP_LOGI(TAG, "clock set by SNTP: %lld", static_cast<long long>(tv->tv_sec));
    }
}

// The HTTP lane's worker thread.
void on_date_header(const char* value) {
    if (s_set.load()) {
        return;
    }
    std::time_t epoch_s = 0;
    if (!helix::format::parse_http_date(value, epoch_s) || s_set.exchange(true)) {
        return;
    }
    if (s_sntp_synced.load()) {
        return; // SNTP answered while this header was parsed; its time is better.
    }
    timeval tv{};
    tv.tv_sec = epoch_s;
    settimeofday(&tv, nullptr);
    ESP_LOGI(TAG, "clock set from Moonraker's Date header: %lld", static_cast<long long>(epoch_s));
}

} // namespace

void start() {
    helix::http::EspHttpLane::set_date_header_hook(&on_date_header);

    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    cfg.sync_cb = &on_sntp_sync;
    esp_err_t rc = esp_netif_sntp_init(&cfg);
    if (rc != ESP_OK) {
        ESP_LOGW(TAG, "esp_netif_sntp_init: %s", esp_err_to_name(rc));
    }
}

void request_date(const std::string& http_base_url) {
    if (s_set.load() || http_base_url.empty()) {
        return;
    }
    // Only the headers matter; the body is discarded, failures included.
    helix::http::EspHttpLane::instance().submit_get(http_base_url + "/server/info", 4096, nullptr,
                                                    nullptr);
}

} // namespace helix::wall_clock_esp
