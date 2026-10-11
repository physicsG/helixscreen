// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/**
 * @file bt_discovery_run.h
 * @brief A plugin context shared with worker threads, and a discovery scan run on one.
 *
 * Both Bluetooth settings overlays scan the same way: helix_bt_discover() blocks for the
 * whole scan, so it runs on a detached worker, and its C callback fires on the plugin's
 * bus thread. The worker can outlive a Stop, a rescan, and the overlay itself, so
 * everything it touches is owned through shared_ptr here rather than by the overlay.
 */

#include "async_lifetime_guard.h"
#include "bluetooth_plugin.h"

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

namespace helix::bluetooth {

/// One plugin context shared by an owner and the workers it spawns. init() runs on the
/// first get(), from whichever thread asks; deinit() runs when the last owner lets go,
/// so a worker still inside a plugin call keeps the context alive.
class SharedContext {
  public:
    SharedContext() = default;
    ~SharedContext();
    SharedContext(const SharedContext&) = delete;
    SharedContext& operator=(const SharedContext&) = delete;

    /// The context, created on the first call; null when init fails or the plugin is
    /// absent. init() makes D-Bus calls, so call this from a worker, not the UI thread.
    helix_bt_context* get();

    /// The context if one exists, without creating it. Never blocks.
    helix_bt_context* peek() const {
        return ctx_.load();
    }

  private:
    std::mutex init_mutex_;
    std::atomic<helix_bt_context*> ctx_{nullptr};
    // Captured with the context so the destructor never reaches BluetoothLoader, which a
    // release during static destruction can outlive.
    helix_bt_deinit_fn deinit_ = nullptr;
};

/// A device reported by discovery, copied out of the plugin's temporaries.
struct DiscoveredDevice {
    std::string mac;
    std::string name;
    bool paired = false;
    bool is_ble = false;
    bool is_scanner = false;
};

/// One helix_bt_discover() on a detached worker, reported on the UI thread.
class DiscoveryRun {
  public:
    struct Callbacks {
        /// Worker thread: whether to report a device. Null reports every device.
        std::function<bool(const helix_bt_device&)> accept;
        /// UI thread, once per reported device.
        std::function<void(const DiscoveredDevice&)> on_device;
        /// UI thread, when the scan ends; false when no context could be created or the
        /// plugin's discover() failed (no adapter, StartDiscovery refused).
        std::function<void(bool ok)> on_finished;
    };

    /// Starts a scan and cancels any earlier one, as cancel() does. Callbacks run only while @p
    /// token is alive and until cancel(). False when the worker thread could not be spawned.
    bool start(std::shared_ptr<SharedContext> ctx, int timeout_ms, LifetimeToken token,
               Callbacks callbacks);

    /// Drops the current scan's remaining callbacks and ends that scan in the plugin, even one
    /// not started yet. Other scans on the same context keep running.
    void cancel();

  private:
    struct State;
    static void report_device(const helix_bt_device* dev, void* user_data);

    std::shared_ptr<State> state_;
};

} // namespace helix::bluetooth
