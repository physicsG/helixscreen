// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

// The LAN sweep's platform pieces on the ESP32 firmware (lan_sweep.h).
//
// The desktop HttpExecutor never starts here, so the sweep gets a few threads of its
// own. Their stacks go in PSRAM: internal RAM is what WiFi and the WebSocket task
// need contiguous, and a long-lived stack there splits its largest block. The limits
// keep the sweep gentle on one radio: three probes at once, a rest after each.

#include "esp_log.h"
#include "esp_netif.h"
#include "lan_sweep.h"
#include "lwip/inet.h"
#include "psram_thread_stack.h"

#include <memory>
#include <pthread.h>

namespace helix {

namespace {

constexpr const char* kTag = "lan_sweep";

struct WorkerStart {
    std::function<void()> body;
};

void* worker_main(void* arg) {
    std::unique_ptr<WorkerStart> start(static_cast<WorkerStart*>(arg));
    psram_thread_entered("lan_sweep");
    start->body();
    return nullptr;
}

} // namespace

// The firmware has no mDNS, so the sweep is the whole discovery.
std::unique_ptr<IMdnsDiscovery> make_moonraker_discovery() {
    return std::make_unique<LanSweepDiscovery>();
}

LanSweepLimits lan_sweep_default_limits() {
    LanSweepLimits limits;
    limits.max_concurrent = 3;
    limits.connect_timeout = std::chrono::milliseconds(300);
    limits.total_timeout = std::chrono::milliseconds(1200);
    limits.pause_between_probes = std::chrono::milliseconds(50);
    limits.worker_stack_bytes = 12 * 1024;
    return limits;
}

std::optional<LocalIpv4> active_local_ipv4() {
    esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!netif) {
        return std::nullopt;
    }
    esp_netif_ip_info_t info{};
    if (esp_netif_get_ip_info(netif, &info) != ESP_OK || info.ip.addr == 0) {
        return std::nullopt;
    }
    return LocalIpv4{lwip_ntohl(info.ip.addr), lwip_ntohl(info.netmask.addr)};
}

LanSweepWorkers start_lan_sweep_workers(size_t count, const LanSweepLimits& limits,
                                        std::function<void()> body) {
    size_t started = 0;
    for (size_t i = 0; i < count; ++i) {
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setstacksize(&attr, limits.worker_stack_bytes);
        // Detached: a worker holds only the sweep's shared state and ends on its own
        // within one probe of the sweep being cancelled.
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        auto* start = new WorkerStart{body};
        pthread_t thread;
        int rc;
        {
            PsramThreadStackScope psram_stack("lan_sweep", limits.worker_stack_bytes);
            // THREAD_OK: PSRAM-stack pthread; std::thread aborts on failure without exceptions
            rc = pthread_create(&thread, &attr, &worker_main, start);
        }
        pthread_attr_destroy(&attr);
        if (rc != 0) {
            ESP_LOGW(kTag, "pthread_create failed: %d", rc);
            delete start;
            break;
        }
        ++started;
    }
    return LanSweepWorkers{nullptr, started};
}

} // namespace helix
