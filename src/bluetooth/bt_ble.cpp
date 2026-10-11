// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file bt_ble.cpp
 * @brief BLE GATT connect, write, and disconnect via BlueZ D-Bus.
 *
 * Used by Phomemo printers that expose a BLE GATT write characteristic.
 * Attempts AcquireWrite for fast fd-based writes, falls back to WriteValue
 * D-Bus method calls per chunk.
 *
 * Also implements the unified helix_bt_disconnect() that handles both
 * RFCOMM fds and BLE handles (tagged with BLE_HANDLE_TAG).
 *
 * All sd-bus operations are serialized through the shared BusThread so they
 * never race with discovery, pairing, or other plugin bus users.
 */

#include "bluetooth_plugin.h"
#include "bt_context.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <poll.h>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

/// Wait for ServicesResolved to become true, polling the property via the
/// BusThread. No nested sd_bus_process loop — BusThread drives the bus.
/// Timeout: 10s.
static int wait_for_services_resolved(helix_bt_context* ctx, const std::string& device_path) {
    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);

    while (true) {
        int resolved = 0;
        int r = 0;
        ctx->bus_thread->run_sync([&](sd_bus* bus) {
            sd_bus_error error = SD_BUS_ERROR_NULL;
            r = sd_bus_get_property_trivial(bus, "org.bluez", device_path.c_str(),
                                            "org.bluez.Device1", "ServicesResolved", &error, 'b',
                                            &resolved);
            sd_bus_error_free(&error);
        });

        if (r >= 0 && resolved) {
            return 0;
        }

        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        long elapsed_ms =
            (now.tv_sec - start.tv_sec) * 1000 + (now.tv_nsec - start.tv_nsec) / 1000000;
        if (elapsed_ms >= 10000) {
            return -ETIMEDOUT;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
}

/// Find a GATT characteristic by UUID under the device path.
/// Must be called from inside a run_sync lambda (executes on the bus thread).
static std::string find_gatt_char(sd_bus* bus, const char* device_path, const char* target_uuid) {
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message* reply = nullptr;
    std::string char_path;

    int r = sd_bus_call_method(bus, "org.bluez", "/", "org.freedesktop.DBus.ObjectManager",
                               "GetManagedObjects", &error, &reply, "");
    if (r < 0) {
        sd_bus_error_free(&error);
        return {};
    }

    std::string dev_prefix(device_path);

    // Parse: a{oa{sa{sv}}}
    r = sd_bus_message_enter_container(reply, 'a', "{oa{sa{sv}}}");
    if (r < 0)
        goto done;

    while ((r = sd_bus_message_enter_container(reply, 'e', "oa{sa{sv}}")) > 0) {
        const char* path = nullptr;
        r = sd_bus_message_read(reply, "o", &path);
        if (r < 0) {
            sd_bus_message_exit_container(reply);
            continue;
        }

        // Only look at objects under our device path
        std::string p(path ? path : "");
        if (p.find(dev_prefix) != 0) {
            sd_bus_message_skip(reply, "a{sa{sv}}");
            sd_bus_message_exit_container(reply);
            continue;
        }

        // Check interfaces
        r = sd_bus_message_enter_container(reply, 'a', "{sa{sv}}");
        if (r < 0) {
            sd_bus_message_exit_container(reply);
            continue;
        }

        while ((r = sd_bus_message_enter_container(reply, 'e', "sa{sv}")) > 0) {
            const char* iface = nullptr;
            r = sd_bus_message_read(reply, "s", &iface);
            if (r < 0 || !iface || strcmp(iface, "org.bluez.GattCharacteristic1") != 0) {
                sd_bus_message_skip(reply, "a{sv}");
                sd_bus_message_exit_container(reply);
                continue;
            }

            // Parse properties to find UUID
            r = sd_bus_message_enter_container(reply, 'a', "{sv}");
            if (r < 0) {
                sd_bus_message_exit_container(reply);
                continue;
            }

            while ((r = sd_bus_message_enter_container(reply, 'e', "sv")) > 0) {
                const char* prop = nullptr;
                r = sd_bus_message_read(reply, "s", &prop);
                if (r >= 0 && prop && strcmp(prop, "UUID") == 0) {
                    const char* val = nullptr;
                    r = sd_bus_message_enter_container(reply, 'v', "s");
                    if (r >= 0) {
                        sd_bus_message_read(reply, "s", &val);
                        if (val && strcasecmp(val, target_uuid) == 0) {
                            char_path = p;
                        }
                        sd_bus_message_exit_container(reply);
                    }
                } else {
                    sd_bus_message_skip(reply, "v");
                }
                sd_bus_message_exit_container(reply);
            }
            sd_bus_message_exit_container(reply); // properties array
            sd_bus_message_exit_container(reply); // interface entry

            if (!char_path.empty())
                break;
        }
        sd_bus_message_exit_container(reply); // interfaces array
        sd_bus_message_exit_container(reply); // object entry

        if (!char_path.empty())
            break;
    }
    sd_bus_message_exit_container(reply);

done:
    sd_bus_message_unref(reply);
    sd_bus_error_free(&error);
    return char_path;
}

/// Try AcquireWrite on a GATT characteristic (BlueZ 5.46+)
/// Must be called from inside a run_sync lambda.
/// Returns fd on success, -1 on failure (fallback to WriteValue)
static int try_acquire_write(sd_bus* bus, const char* char_path, uint16_t* out_mtu) {
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message* reply = nullptr;

    // AcquireWrite takes options dict: a{sv}
    int r = sd_bus_call_method(bus, "org.bluez", char_path, "org.bluez.GattCharacteristic1",
                               "AcquireWrite", &error, &reply, "a{sv}", 0); // empty options dict
    if (r < 0) {
        sd_bus_error_free(&error);
        return -1;
    }

    int fd = -1;
    uint16_t mtu = 20;
    r = sd_bus_message_read(reply, "hq", &fd, &mtu);
    if (r < 0) {
        sd_bus_message_unref(reply);
        sd_bus_error_free(&error);
        return -1;
    }

    // fd from sd-bus is borrowed — dup it to own it
    int owned_fd = dup(fd);
    sd_bus_message_unref(reply);
    sd_bus_error_free(&error);

    if (owned_fd < 0)
        return -1;

    if (out_mtu)
        *out_mtu = mtu;
    fprintf(stderr, "[bt] AcquireWrite succeeded (fd=%d, mtu=%u)\n", owned_fd, mtu);
    return owned_fd;
}

/// Try AcquireNotify on a GATT characteristic (BlueZ 5.46+)
/// Must be called from inside a run_sync lambda.
/// Returns fd on success, -1 on failure. On failure falls back to StartNotify
/// (values then arrive as PropertiesChanged signals, not via fd).
static int try_acquire_notify(sd_bus* bus, const char* char_path) {
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message* reply = nullptr;

    int r = sd_bus_call_method(bus, "org.bluez", char_path, "org.bluez.GattCharacteristic1",
                               "AcquireNotify", &error, &reply, "a{sv}", 0); // empty options dict
    if (r < 0) {
        fprintf(stderr, "[bt] AcquireNotify failed: %s (falling back to StartNotify)\n",
                error.message ? error.message : strerror(-r));
        sd_bus_error_free(&error);

        // Fallback: try StartNotify (older BlueZ / different characteristic flags)
        error = SD_BUS_ERROR_NULL;
        r = sd_bus_call_method(bus, "org.bluez", char_path, "org.bluez.GattCharacteristic1",
                               "StartNotify", &error, nullptr, "");
        sd_bus_error_free(&error);
        if (r < 0) {
            fprintf(stderr, "[bt] StartNotify also failed: %s\n", strerror(-r));
        } else {
            fprintf(stderr, "[bt] StartNotify succeeded (values arrive via PropertiesChanged)\n");
        }
        return -1;
    }

    int fd = -1;
    uint16_t mtu = 20;
    r = sd_bus_message_read(reply, "hq", &fd, &mtu);
    if (r < 0) {
        sd_bus_message_unref(reply);
        sd_bus_error_free(&error);
        return -1;
    }

    // fd from sd-bus is borrowed — dup it to own it
    int owned_fd = dup(fd);
    sd_bus_message_unref(reply);
    sd_bus_error_free(&error);

    if (owned_fd < 0)
        return -1;

    fprintf(stderr, "[bt] AcquireNotify succeeded (fd=%d, mtu=%u)\n", owned_fd, mtu);
    return owned_fd;
}

/// Read MTU property from the device (fallback value if not available)
static uint16_t read_mtu(sd_bus* bus, const char* device_path) {
    // BlueZ exposes MTU on the characteristic after AcquireWrite, but if we
    // didn't get it that way, use the default BLE minimum (20).
    (void)bus;
    (void)device_path;
    return 20;
}

/// PropertiesChanged signal handler for a GATT characteristic.
/// BlueZ emits these when StartNotify is active (no AcquireNotify fd).
/// Signature: s (interface) a{sv} (changed_properties) as (invalidated)
static int on_notify_changed(sd_bus_message* msg, void* userdata, sd_bus_error* /*ret_error*/) {
    auto* conn = static_cast<helix_bt_context::BleConnection*>(userdata);
    if (!conn)
        return 0;

    const char* iface = nullptr;
    int r = sd_bus_message_read(msg, "s", &iface);
    if (r < 0 || !iface)
        return 0;
    if (strcmp(iface, "org.bluez.GattCharacteristic1") != 0) {
        return 0;
    }

    // changed_properties: a{sv}
    r = sd_bus_message_enter_container(msg, 'a', "{sv}");
    if (r < 0)
        return 0;

    std::vector<uint8_t> value;
    bool have_value = false;

    while ((r = sd_bus_message_enter_container(msg, 'e', "sv")) > 0) {
        const char* prop = nullptr;
        r = sd_bus_message_read(msg, "s", &prop);
        if (r < 0 || !prop) {
            sd_bus_message_exit_container(msg);
            continue;
        }

        if (strcmp(prop, "Value") == 0) {
            r = sd_bus_message_enter_container(msg, 'v', "ay");
            if (r >= 0) {
                const void* bytes = nullptr;
                size_t n = 0;
                r = sd_bus_message_read_array(msg, 'y', &bytes, &n);
                if (r >= 0 && bytes && n > 0) {
                    value.assign(static_cast<const uint8_t*>(bytes),
                                 static_cast<const uint8_t*>(bytes) + n);
                    have_value = true;
                }
                sd_bus_message_exit_container(msg);
            }
        } else {
            sd_bus_message_skip(msg, "v");
        }

        sd_bus_message_exit_container(msg); // sv entry
    }
    sd_bus_message_exit_container(msg); // a{sv}

    if (have_value) {
        std::lock_guard<std::mutex> lock(conn->rx_mu);
        conn->rx_queue.push_back(std::move(value));
        conn->rx_cv.notify_one();
    }
    return 0;
}

/// Stores a connection in the first free slot and returns its handle.
static int ble_store_connection(helix_bt_context* ctx,
                                std::shared_ptr<helix_bt_context::BleConnection> conn) {
    std::lock_guard<std::mutex> lock(ctx->ble_mutex);
    auto& table = ctx->ble_connections;
    auto slot = std::find(table.begin(), table.end(), nullptr);
    if (slot == table.end())
        slot = table.insert(table.end(), nullptr);
    *slot = std::move(conn);
    return helix::bluetooth::BLE_HANDLE_TAG | static_cast<int>(slot - table.begin());
}

/// The live connection behind a handle, or null for a stale or foreign handle.
static std::shared_ptr<helix_bt_context::BleConnection> ble_find_connection(helix_bt_context* ctx,
                                                                            int handle) {
    if (!helix::bluetooth::is_ble_handle(handle))
        return nullptr;
    size_t index = static_cast<size_t>(handle & ~helix::bluetooth::BLE_HANDLE_TAG);
    std::lock_guard<std::mutex> lock(ctx->ble_mutex);
    if (index >= ctx->ble_connections.size())
        return nullptr;
    return ctx->ble_connections[index];
}

// ---------------------------------------------------------------------------
// Public API: BLE Connect
// ---------------------------------------------------------------------------

extern "C" int helix_bt_connect_ble(helix_bt_context* ctx, const char* mac,
                                    const char* write_uuid) {
    if (!ctx)
        return -EINVAL;
    if (!mac || !write_uuid) {
        std::lock_guard<std::mutex> lock(ctx->mutex);
        ctx->last_error = "null MAC or UUID";
        return -EINVAL;
    }
    if (!ctx->bus || !ctx->bus_thread) {
        std::lock_guard<std::mutex> lock(ctx->mutex);
        ctx->last_error = "bus not initialized";
        return -ENODEV;
    }

    std::string device_path;
    try {
        ctx->bus_thread->run_sync(
            [&](sd_bus* bus) { device_path = helix::bluetooth::device_dbus_path(bus, mac); });
    } catch (const std::exception& e) {
        std::lock_guard<std::mutex> lock(ctx->mutex);
        ctx->last_error = e.what();
        return -EIO;
    }
    fprintf(stderr, "[bt] BLE connecting to %s (%s)\n", mac, device_path.c_str());

    int r = 0;
    std::string err;
    bool fatal_connect_failure = false;

    // Step 1: Connect() to the device.
    //
    // org.bluez.Device1.Connect() lets BlueZ pick transport (BR/EDR first when
    // the device is cached as dual-mode). For BLE-only devices that were
    // previously paired classic or whose advertising confuses BlueZ, this
    // produces "br-connection-profile-unavailable" even though the LE GATT
    // service is present. When we detect that failure mode we retry with
    // ConnectProfile(write_uuid), which forces the LE transport for the
    // specific GATT characteristic UUID we actually care about.
    auto do_connect = [&](const char* method, const char* signature, const char* uuid_arg) {
        err.clear();
        fatal_connect_failure = false;
        try {
            ctx->bus_thread->run_sync([&](sd_bus* bus) {
                sd_bus_error error = SD_BUS_ERROR_NULL;
                if (signature && uuid_arg) {
                    r = sd_bus_call_method(bus, "org.bluez", device_path.c_str(),
                                           "org.bluez.Device1", method, &error, nullptr, signature,
                                           uuid_arg);
                } else {
                    r = sd_bus_call_method(bus, "org.bluez", device_path.c_str(),
                                           "org.bluez.Device1", method, &error, nullptr, "");
                }
                if (r < 0 && !sd_bus_error_has_name(&error, "org.bluez.Error.AlreadyConnected")) {
                    err = error.message ? error.message : "BLE connect failed";
                    fatal_connect_failure = true;
                } else {
                    r = 0;
                }
                sd_bus_error_free(&error);
            });
        } catch (const std::exception& e) {
            err = e.what();
            r = -EIO;
            fatal_connect_failure = true;
        }
    };

    do_connect("Connect", nullptr, nullptr);

    // Known BlueZ BR/EDR transport failures — fall back to ConnectProfile(uuid)
    // which is LE-only for GATT service UUIDs. Strings come from
    // btd_error_*() in bluez's src/error.c and all carry the "br-connection-"
    // prefix.
    if (fatal_connect_failure && err.find("br-connection-") != std::string::npos) {
        fprintf(stderr, "[bt] Device1.Connect failed (%s), retrying via ConnectProfile(%s)\n",
                err.c_str(), write_uuid);
        do_connect("ConnectProfile", "s", write_uuid);
    }

    if (fatal_connect_failure) {
        fprintf(stderr, "[bt] BLE connect failed for %s: %s\n", mac, err.c_str());
        std::lock_guard<std::mutex> lock(ctx->mutex);
        ctx->last_error = err.empty() ? "BLE connect failed" : err;
        return r < 0 ? r : -EIO;
    }

    // Step 2: Wait for ServicesResolved.
    r = wait_for_services_resolved(ctx, device_path);
    if (r < 0) {
        fprintf(stderr, "[bt] timeout waiting for ServicesResolved on %s\n", mac);
        std::lock_guard<std::mutex> lock(ctx->mutex);
        ctx->last_error = "timeout waiting for BLE services";
        return r;
    }

    // Step 3: Find GATT char, AcquireWrite, AcquireNotify, register signal match —
    // all inside a single run_sync so find_gatt_char and subsequent calls share
    // the bus thread.
    std::string char_path;
    int acquired_fd = -1;
    int notify_fd = -1;
    uint16_t mtu = 20;
    bool used_start_notify = false;

    try {
        ctx->bus_thread->run_sync([&](sd_bus* bus) {
            char_path = find_gatt_char(bus, device_path.c_str(), write_uuid);
            if (char_path.empty()) {
                return;
            }

            acquired_fd = try_acquire_write(bus, char_path.c_str(), &mtu);
            if (acquired_fd < 0) {
                mtu = read_mtu(bus, device_path.c_str());
            }

            notify_fd = try_acquire_notify(bus, char_path.c_str());
            if (notify_fd < 0) {
                // try_acquire_notify fell back to StartNotify (or it failed).
                // Either way, we'll register a PropertiesChanged match below
                // so values arriving via signal can be queued.
                used_start_notify = true;
            }
        });
    } catch (const std::exception& e) {
        err = e.what();
        std::lock_guard<std::mutex> lock(ctx->mutex);
        ctx->last_error = err;
        return -EIO;
    }

    if (char_path.empty()) {
        fprintf(stderr, "[bt] GATT characteristic %s not found on %s\n", write_uuid, mac);
        std::lock_guard<std::mutex> lock(ctx->mutex);
        ctx->last_error = "GATT characteristic not found";
        return -ENOENT;
    }

    fprintf(stderr, "[bt] found GATT char: %s\n", char_path.c_str());
    if (acquired_fd < 0) {
        fprintf(stderr, "[bt] AcquireWrite unavailable, using WriteValue (mtu=%u)\n", mtu);
    }

    // Step 4: Create BleConnection and register PropertiesChanged match if needed.
    auto conn = std::make_shared<helix_bt_context::BleConnection>();
    conn->device_path = device_path;
    conn->char_path = char_path;
    conn->acquired_fd = acquired_fd;
    conn->notify_fd = notify_fd;
    conn->mtu = mtu;
    conn->active = true;

    helix_bt_context::BleConnection* conn_raw = conn.get();
    int handle = ble_store_connection(ctx, std::move(conn));

    // With an AcquireNotify fd, BlueZ delivers bytes via the fd and does NOT emit
    // PropertiesChanged for Value. We only need the signal match when we fell back
    // to StartNotify (no fd), which DOES emit PropertiesChanged.
    // The table keeps conn_raw alive until disconnect has unreffed this match.
    if (notify_fd < 0) {
        try {
            ctx->bus_thread->run_sync([&](sd_bus* bus) {
                int mr = sd_bus_match_signal(bus, &conn_raw->notify_slot, "org.bluez",
                                             conn_raw->char_path.c_str(),
                                             "org.freedesktop.DBus.Properties", "PropertiesChanged",
                                             on_notify_changed, conn_raw);
                if (mr < 0) {
                    fprintf(stderr, "[bt] sd_bus_match_signal for PropertiesChanged failed: %s\n",
                            strerror(-mr));
                    conn_raw->notify_slot = nullptr;
                }
            });
        } catch (const std::exception& e) {
            fprintf(stderr, "[bt] exception registering signal match: %s\n", e.what());
        }
    }

    fprintf(stderr, "[bt] BLE connected to %s (handle=%d, write_fd=%d, notify_fd=%d, mtu=%u%s)\n",
            mac, handle, acquired_fd, notify_fd, mtu,
            used_start_notify ? ", signal-based notify" : "");
    return handle;
}

// ---------------------------------------------------------------------------
// Public API: BLE Write
// ---------------------------------------------------------------------------

extern "C" int helix_bt_ble_write(helix_bt_context* ctx, int handle, const uint8_t* data, int len) {
    if (!ctx)
        return -EINVAL;
    if (!data || len <= 0) {
        std::lock_guard<std::mutex> lock(ctx->mutex);
        ctx->last_error = "null data or zero length";
        return -EINVAL;
    }
    if (!helix::bluetooth::is_ble_handle(handle)) {
        std::lock_guard<std::mutex> lock(ctx->mutex);
        ctx->last_error = "invalid BLE handle";
        return -EINVAL;
    }

    // A local shared_ptr keeps the connection alive without holding ble_mutex across the
    // sd-bus work below (that would hold it and the bus thread's queue mutex together).
    auto conn = ble_find_connection(ctx, handle);
    if (!conn || !conn->active) {
        std::lock_guard<std::mutex> lock2(ctx->mutex);
        ctx->last_error = "BLE connection not active";
        return -ENOTCONN;
    }

    // Effective payload per chunk: MTU - 3 (ATT header)
    int chunk_size = conn->mtu > 3 ? conn->mtu - 3 : conn->mtu;
    if (chunk_size <= 0)
        chunk_size = 20;

    int offset = 0;
    while (offset < len) {
        int remaining = len - offset;
        int to_write = remaining < chunk_size ? remaining : chunk_size;

        if (conn->acquired_fd >= 0) {
            // Fast path: write directly to acquired fd (no bus thread needed)
            ssize_t written =
                write(conn->acquired_fd, data + offset, static_cast<size_t>(to_write));
            if (written < 0) {
                int err = errno;
                fprintf(stderr, "[bt] BLE fd write failed: %s\n", strerror(err));
                std::lock_guard<std::mutex> lock2(ctx->mutex);
                ctx->last_error = std::string("BLE write failed: ") + strerror(err);
                return -err;
            }
        } else {
            // Slow path: WriteValue D-Bus method call per chunk, on bus thread.
            int r = 0;
            std::string werr;
            try {
                ctx->bus_thread->run_sync([&](sd_bus* bus) {
                    sd_bus_error error = SD_BUS_ERROR_NULL;
                    sd_bus_message* msg = nullptr;

                    r = sd_bus_message_new_method_call(
                        bus, &msg, "org.bluez", conn->char_path.c_str(),
                        "org.bluez.GattCharacteristic1", "WriteValue");
                    if (r < 0) {
                        werr = "failed to create WriteValue message";
                        sd_bus_error_free(&error);
                        return;
                    }

                    // Append data as byte array: ay
                    r = sd_bus_message_append_array(msg, 'y', data + offset,
                                                    static_cast<size_t>(to_write));
                    if (r < 0) {
                        sd_bus_message_unref(msg);
                        werr = "failed to append write data";
                        sd_bus_error_free(&error);
                        return;
                    }

                    // Append options dict: a{sv} (empty)
                    r = sd_bus_message_open_container(msg, 'a', "{sv}");
                    if (r >= 0)
                        r = sd_bus_message_close_container(msg);
                    if (r < 0) {
                        sd_bus_message_unref(msg);
                        werr = "failed to build WriteValue options";
                        sd_bus_error_free(&error);
                        return;
                    }

                    sd_bus_message* reply = nullptr;
                    r = sd_bus_call(bus, msg, 5000000, &error, &reply); // 5s timeout
                    sd_bus_message_unref(msg);
                    if (reply)
                        sd_bus_message_unref(reply);

                    if (r < 0) {
                        werr = error.message ? error.message : "WriteValue failed";
                    }
                    sd_bus_error_free(&error);
                });
            } catch (const std::exception& e) {
                werr = e.what();
                r = -EIO;
            }

            if (r < 0) {
                fprintf(stderr, "[bt] WriteValue failed: %s\n",
                        werr.empty() ? strerror(-r) : werr.c_str());
                std::lock_guard<std::mutex> lock2(ctx->mutex);
                ctx->last_error = werr.empty() ? "WriteValue failed" : werr;
                return r;
            }
        }

        // BLE printers need inter-chunk delay to avoid buffer overflow
        if (offset + to_write < len) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        offset += to_write;
    }

    return 0;
}

// ---------------------------------------------------------------------------
// Public API: BLE Read (via notify fd OR PropertiesChanged signal queue)
// ---------------------------------------------------------------------------

extern "C" int helix_bt_ble_read(helix_bt_context* ctx, int handle, uint8_t* buf, int buf_len,
                                 int timeout_ms) {
    if (!ctx)
        return -EINVAL;
    if (!buf || buf_len <= 0)
        return -EINVAL;
    if (!helix::bluetooth::is_ble_handle(handle))
        return -EINVAL;

    auto conn = ble_find_connection(ctx, handle);
    if (!conn || !conn->active)
        return -ENOTCONN;

    // Prefer the PropertiesChanged-backed queue: the signal handler runs on
    // the bus thread and populates it without any read/poll here. This also
    // works when BlueZ gave us a StartNotify (no fd) or an AcquireNotify fd
    // that hasn't been drained yet (the signal fires regardless).
    {
        std::unique_lock<std::mutex> lk(conn->rx_mu);
        if (conn->rx_cv.wait_for(lk, std::chrono::milliseconds(timeout_ms),
                                 [&conn] { return !conn->rx_queue.empty() || !conn->active; })) {
            if (conn->rx_queue.empty())
                return -ENOTCONN;
            auto data = std::move(conn->rx_queue.front());
            conn->rx_queue.pop_front();
            lk.unlock();
            int n = std::min<int>(buf_len, static_cast<int>(data.size()));
            std::memcpy(buf, data.data(), static_cast<size_t>(n));
            return n;
        }
    }

    // No signal-delivered data arrived. If we have an AcquireNotify fd, poll
    // it briefly (0 ms) in case BlueZ delivered bytes there instead.
    if (conn->notify_fd >= 0) {
        struct pollfd pfd;
        pfd.fd = conn->notify_fd;
        pfd.events = POLLIN;
        pfd.revents = 0;
        int ret = poll(&pfd, 1, 0);
        if (ret > 0 && (pfd.revents & POLLIN)) {
            ssize_t n = read(conn->notify_fd, buf, static_cast<size_t>(buf_len));
            if (n < 0) {
                int err = errno;
                fprintf(stderr, "[bt] BLE read failed: %s\n", strerror(err));
                return -err;
            }
            return static_cast<int>(n);
        }
        if (ret > 0 && (pfd.revents & (POLLERR | POLLHUP | POLLNVAL))) {
            fprintf(stderr, "[bt] BLE read: poll error on notify fd (revents=0x%x)\n", pfd.revents);
            return -EIO;
        }
    }

    return 0; // timeout, no data
}

// ---------------------------------------------------------------------------
// Public API: Disconnect (unified for RFCOMM and BLE)
// ---------------------------------------------------------------------------

extern "C" void helix_bt_disconnect(helix_bt_context* ctx, int handle) {
    if (!ctx)
        return;

    if (helix::bluetooth::is_ble_handle(handle)) {
        auto conn = ble_find_connection(ctx, handle);
        if (!conn || !conn->active.exchange(false))
            return;

        // Wake any reader blocked on rx_cv so it observes !active and bails.
        {
            std::lock_guard<std::mutex> rxlock(conn->rx_mu);
            conn->rx_cv.notify_all();
        }

        // Close acquired fds if held (no bus traffic needed)
        if (conn->acquired_fd >= 0) {
            close(conn->acquired_fd);
            conn->acquired_fd = -1;
        }
        if (conn->notify_fd >= 0) {
            close(conn->notify_fd);
            conn->notify_fd = -1;
        }

        // Unref the signal match and call Device1.Disconnect on the bus thread.
        if (ctx->bus && ctx->bus_thread && !conn->device_path.empty()) {
            try {
                ctx->bus_thread->run_sync([&](sd_bus* bus) {
                    if (conn->notify_slot) {
                        sd_bus_slot_unref(conn->notify_slot);
                        conn->notify_slot = nullptr;
                    }
                    sd_bus_error error = SD_BUS_ERROR_NULL;
                    sd_bus_call_method(bus, "org.bluez", conn->device_path.c_str(),
                                       "org.bluez.Device1", "Disconnect", &error, nullptr, "");
                    sd_bus_error_free(&error);
                });
            } catch (const std::exception& e) {
                fprintf(stderr, "[bt] BLE disconnect exception: %s\n", e.what());
            }
        }

        // A match still registered would call into a freed connection, so its slot stays
        // taken; that only happens when the bus thread is already gone.
        if (!conn->notify_slot) {
            std::lock_guard<std::mutex> lock(ctx->ble_mutex);
            ctx->ble_connections[static_cast<size_t>(handle & ~helix::bluetooth::BLE_HANDLE_TAG)]
                .reset();
        }

        fprintf(stderr, "[bt] BLE disconnected (handle=%d)\n", handle);
    } else {
        // RFCOMM disconnect — handle is the fd
        std::lock_guard<std::mutex> lock(ctx->mutex);
        auto it = ctx->rfcomm_fds.find(handle);
        if (it != ctx->rfcomm_fds.end()) {
            close(handle);
            ctx->rfcomm_fds.erase(it);
            fprintf(stderr, "[bt] RFCOMM disconnected (fd=%d)\n", handle);
        }
    }
}
