// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/**
 * @file bt_context.h
 * @brief Internal context structure for the Bluetooth plugin.
 *
 * This header is NOT installed — it is private to the plugin .so.
 * The main binary only sees the opaque helix_bt_context typedef from bluetooth_plugin.h.
 */

#include "bt_bus_thread.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <systemd/sd-bus.h>
#include <vector>

namespace helix::bluetooth {

/// Object path of the first BlueZ adapter (typically "/org/bluez/hci0"), or empty when
/// BlueZ answers with none. Bus thread only.
std::string find_adapter_path(sd_bus* bus);

/// BlueZ object path of @p mac under the adapter find_adapter_path() reports, falling back
/// to hci0 when there is none so the D-Bus call names a path and fails with BlueZ's error.
/// Bus thread only.
std::string device_dbus_path(sd_bus* bus, const char* mac);

/// Bit set in every BLE handle. connect_rfcomm refuses an fd that carries it, so an RFCOMM
/// fd and a BLE handle can never share a value.
inline constexpr int BLE_HANDLE_TAG = 0x40000000;

inline bool is_ble_handle(int handle) {
    return handle >= 0 && (handle & BLE_HANDLE_TAG) != 0;
}

} // namespace helix::bluetooth

struct helix_bt_context {
    sd_bus* bus = nullptr;
    std::mutex mutex;
    std::string last_error;
    std::atomic<bool> discovering{false};
    sd_bus_slot* discovery_slot = nullptr;
    // One discover() at a time per context: the slot, the flag and the adapter's
    // StartDiscovery/StopDiscovery pair all belong to a single scan.
    std::mutex discover_mutex;
    // Bumped by stop_discovery(), which ends every scan on the context, including one still
    // waiting for discover_mutex. A caller's own cancel flag ends only its scan.
    std::atomic<unsigned> discover_stop_gen{0};
    sd_bus_slot* agent_slot = nullptr;
    std::unique_ptr<helix::bluetooth::BusThread> bus_thread;

    // RFCOMM fd tracking (for safe disconnect)
    std::set<int> rfcomm_fds;

    // BLE connections
    struct BleConnection {
        std::string device_path;
        std::string char_path;
        int acquired_fd = -1;
        int notify_fd = -1;
        uint16_t mtu = 20;
        std::atomic<bool> active{false};

        // PropertiesChanged signal match for GATT notifications (used when
        // AcquireNotify failed and we fell back to StartNotify — values then
        // arrive as PropertiesChanged signals on the characteristic).
        sd_bus_slot* notify_slot = nullptr;
        std::mutex rx_mu;
        std::condition_variable rx_cv;
        std::deque<std::vector<uint8_t>> rx_queue;
    };
    std::mutex ble_mutex;
    // Indexed by handle & ~BLE_HANDLE_TAG. A disconnected slot is null and the next
    // connection reuses it. shared_ptr so a reader or writer that looked a connection up
    // keeps it alive while disconnect frees the slot.
    std::vector<std::shared_ptr<BleConnection>> ble_connections;
};

/// Register/unregister the BlueZ Agent1 for "Just Works" pairing.
/// Called from init/deinit — not part of the dlsym ABI.
extern "C" int helix_bt_register_agent(helix_bt_context* ctx);
extern "C" void helix_bt_unregister_agent(helix_bt_context* ctx);
