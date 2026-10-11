// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file bt_discovery.cpp
 * @brief BlueZ D-Bus device discovery via sd-bus.
 *
 * Discovers Bluetooth devices advertising label printer services:
 *   - SPP UUID 00001101-0000-1000-8000-00805f9b34fb (Brother QL Classic)
 *   - Phomemo UUID 0000ff00-0000-1000-8000-00805f9b34fb (Phomemo BLE)
 *
 * The discover function blocks the calling thread until timeout or stop, while
 * the plugin's BusThread drives sd_bus_process so InterfacesAdded signals fire.
 * All sd-bus calls run via ctx->bus_thread->run_sync() for thread-safety.
 */

#include "bluetooth_plugin.h"
#include "bt_context.h"
#include "bt_discovery_utils.h"
#include "bt_scanner_discovery_utils.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <thread>
#include <vector>

using helix::bluetooth::is_label_printer_uuid;
using helix::bluetooth::uuid_is_ble;

/// State passed through the signal callback
struct discover_ctx {
    helix_bt_context* bt;
    helix_bt_discover_cb cb;
    void* user_data;
};

/// Parse a single device's properties from a D-Bus message iterator and invoke callback
/// if it matches a label printer UUID.
/// The message iterator should be positioned at the properties dict for
/// "org.bluez.Device1" interface.
static void parse_device_properties(sd_bus_message* msg, discover_ctx* dctx) {
    int r;
    // We are inside a dict entry for an interface.
    // The properties are an array of dict entries: a{sv}
    r = sd_bus_message_enter_container(msg, 'a', "{sv}");
    if (r < 0)
        return;

    std::string address, name;
    bool paired = false;
    std::vector<std::string> uuids;

    while ((r = sd_bus_message_enter_container(msg, 'e', "sv")) > 0) {
        const char* prop = nullptr;
        r = sd_bus_message_read(msg, "s", &prop);
        if (r < 0) {
            sd_bus_message_exit_container(msg);
            continue;
        }

        if (strcmp(prop, "Address") == 0) {
            const char* val = nullptr;
            r = sd_bus_message_enter_container(msg, 'v', "s");
            if (r >= 0) {
                sd_bus_message_read(msg, "s", &val);
                if (val)
                    address = val;
                sd_bus_message_exit_container(msg);
            }
        } else if (strcmp(prop, "Name") == 0 || strcmp(prop, "Alias") == 0) {
            const char* val = nullptr;
            r = sd_bus_message_enter_container(msg, 'v', "s");
            if (r >= 0) {
                sd_bus_message_read(msg, "s", &val);
                if (val && name.empty())
                    name = val;
                sd_bus_message_exit_container(msg);
            }
        } else if (strcmp(prop, "Paired") == 0) {
            r = sd_bus_message_enter_container(msg, 'v', "b");
            if (r >= 0) {
                int val = 0;
                sd_bus_message_read(msg, "b", &val);
                paired = (val != 0);
                sd_bus_message_exit_container(msg);
            }
        } else if (strcmp(prop, "UUIDs") == 0) {
            r = sd_bus_message_enter_container(msg, 'v', "as");
            if (r >= 0) {
                r = sd_bus_message_enter_container(msg, 'a', "s");
                if (r >= 0) {
                    const char* val = nullptr;
                    while (sd_bus_message_read(msg, "s", &val) > 0) {
                        if (val)
                            uuids.emplace_back(val);
                    }
                    sd_bus_message_exit_container(msg);
                }
                sd_bus_message_exit_container(msg);
            }
        } else {
            // Skip unknown properties
            sd_bus_message_skip(msg, "v");
        }

        sd_bus_message_exit_container(msg); // dict entry "sv"
    }
    sd_bus_message_exit_container(msg); // array "a{sv}"

    // Skip devices with no name (raw MAC-only entries are not useful)
    if (address.empty())
        return;
    bool has_real_name = !name.empty() && name != address;

    // Determine BLE vs Classic from UUIDs if available
    bool is_ble = false;
    bool has_printer_uuid = false;
    std::string matching_uuid;
    for (const auto& uuid : uuids) {
        if (is_label_printer_uuid(uuid.c_str())) {
            matching_uuid = uuid;
            is_ble = uuid_is_ble(uuid.c_str());
            has_printer_uuid = true;
            break;
        }
    }

    // Show a device that looks like a printer or a scanner, or that the user already paired.
    const auto traits =
        helix::bluetooth::classify_device(has_real_name ? name.c_str() : nullptr, uuids);
    bool dominated_by_paired = paired && has_real_name;

    if (!traits.printer_uuid && !traits.printer_name && !dominated_by_paired && !traits.scanner) {
        fprintf(stderr, "[bt] filtered: %s '%s' (not a likely printer or scanner)\n",
                address.c_str(), name.c_str());
        return;
    }

    fprintf(stderr, "[bt] accept: %s '%s' (uuid=%d name=%d paired=%d scanner=%d)\n",
            address.c_str(), name.c_str(), traits.printer_uuid, traits.printer_name,
            dominated_by_paired, traits.scanner);

    // Name-based BLE override: some BLE printers (e.g. MakeID/YichipFPGA) also
    // advertise SPP UUID but actually communicate via BLE GATT. The brand table
    // knows which devices are BLE-only, so let it override UUID-based detection.
    if (has_real_name && helix::bluetooth::name_suggests_ble(name.c_str())) {
        is_ble = true;
    } else if (!has_printer_uuid && has_real_name) {
        is_ble = helix::bluetooth::name_suggests_ble(name.c_str());
    }

    helix_bt_device dev = {};
    dev.mac = address.c_str();
    dev.name = has_real_name ? name.c_str() : address.c_str();
    dev.paired = paired;
    dev.is_ble = is_ble;
    dev.service_uuid = matching_uuid.empty() ? nullptr : matching_uuid.c_str();
    dev.is_scanner = traits.is_scanner();

    if (dctx->cb) {
        dctx->cb(&dev, dctx->user_data);
    }
}

/// Signal handler for org.freedesktop.DBus.ObjectManager.InterfacesAdded
static int on_interfaces_added(sd_bus_message* msg, void* userdata, sd_bus_error* /*ret_error*/) {
    auto* dctx = static_cast<discover_ctx*>(userdata);
    if (!dctx || !dctx->bt->discovering.load())
        return 0;

    int r;
    const char* path = nullptr;
    r = sd_bus_message_read(msg, "o", &path);
    if (r < 0)
        return 0;

    // Enter interfaces dict: a{sa{sv}}
    r = sd_bus_message_enter_container(msg, 'a', "{sa{sv}}");
    if (r < 0)
        return 0;

    while ((r = sd_bus_message_enter_container(msg, 'e', "sa{sv}")) > 0) {
        const char* iface = nullptr;
        r = sd_bus_message_read(msg, "s", &iface);
        if (r < 0) {
            sd_bus_message_exit_container(msg);
            continue;
        }

        if (strcmp(iface, "org.bluez.Device1") == 0) {
            parse_device_properties(msg, dctx);
        } else {
            // Skip properties for other interfaces
            sd_bus_message_skip(msg, "a{sv}");
        }

        sd_bus_message_exit_container(msg); // dict entry
    }
    sd_bus_message_exit_container(msg); // array

    return 0;
}

std::string helix::bluetooth::find_adapter_path(sd_bus* bus) {
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message* reply = nullptr;

    int r = sd_bus_call_method(bus, "org.bluez", "/", "org.freedesktop.DBus.ObjectManager",
                               "GetManagedObjects", &error, &reply, "");
    if (r < 0) {
        sd_bus_error_free(&error);
        return {};
    }

    std::string adapter_path;

    // Parse response: a{oa{sa{sv}}}
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

        // Check interfaces for Adapter1
        r = sd_bus_message_enter_container(reply, 'a', "{sa{sv}}");
        if (r < 0) {
            sd_bus_message_exit_container(reply);
            continue;
        }

        while ((r = sd_bus_message_enter_container(reply, 'e', "sa{sv}")) > 0) {
            const char* iface = nullptr;
            r = sd_bus_message_read(reply, "s", &iface);
            if (r >= 0 && iface && strcmp(iface, "org.bluez.Adapter1") == 0) {
                adapter_path = path;
            }
            sd_bus_message_skip(reply, "a{sv}");
            sd_bus_message_exit_container(reply);
        }
        sd_bus_message_exit_container(reply); // interfaces array
        sd_bus_message_exit_container(reply); // object entry

        if (!adapter_path.empty())
            break;
    }
    sd_bus_message_exit_container(reply); // top-level array

done:
    sd_bus_message_unref(reply);
    sd_bus_error_free(&error);
    return adapter_path;
}

/// Enumerate already-known devices from BlueZ's object manager
static void enumerate_known_devices(sd_bus* bus, discover_ctx* dctx) {
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message* reply = nullptr;

    int r = sd_bus_call_method(bus, "org.bluez", "/", "org.freedesktop.DBus.ObjectManager",
                               "GetManagedObjects", &error, &reply, "");
    if (r < 0) {
        sd_bus_error_free(&error);
        return;
    }

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

        // Check interfaces for Device1
        r = sd_bus_message_enter_container(reply, 'a', "{sa{sv}}");
        if (r < 0) {
            sd_bus_message_exit_container(reply);
            continue;
        }

        while ((r = sd_bus_message_enter_container(reply, 'e', "sa{sv}")) > 0) {
            const char* iface = nullptr;
            r = sd_bus_message_read(reply, "s", &iface);
            if (r >= 0 && iface && strcmp(iface, "org.bluez.Device1") == 0) {
                parse_device_properties(reply, dctx);
            } else {
                sd_bus_message_skip(reply, "a{sv}");
            }
            sd_bus_message_exit_container(reply);
        }
        sd_bus_message_exit_container(reply); // interfaces array
        sd_bus_message_exit_container(reply); // object entry
    }
    sd_bus_message_exit_container(reply);

done:
    sd_bus_message_unref(reply);
    sd_bus_error_free(&error);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

extern "C" int helix_bt_discover(helix_bt_context* ctx, int timeout_ms, helix_bt_discover_cb cb,
                                 void* user_data, const int* cancel) {
    if (!ctx || !ctx->bus_thread)
        return -EINVAL;

    const unsigned stop_gen = ctx->discover_stop_gen.load();
    auto stopped = [&] {
        return (cancel && __atomic_load_n(cancel, __ATOMIC_ACQUIRE) != 0) ||
               ctx->discover_stop_gen.load() != stop_gen;
    };
    std::lock_guard<std::mutex> one_scan(ctx->discover_mutex);
    if (stopped())
        return 0; // stopped before it started

    auto* dctx = new (std::nothrow) discover_ctx{ctx, cb, user_data};
    if (!dctx)
        return -ENOMEM;

    ctx->discovering.store(true);

    int setup_result = 0;
    std::string adapter;

    try {
        ctx->bus_thread->run_sync([&](sd_bus* bus) {
            adapter = helix::bluetooth::find_adapter_path(bus);
            if (adapter.empty()) {
                std::lock_guard<std::mutex> lock(ctx->mutex);
                ctx->last_error = "no BlueZ adapter found";
                setup_result = -ENODEV;
                return;
            }
            fprintf(stderr, "[bt] using adapter: %s\n", adapter.c_str());

            enumerate_known_devices(bus, dctx);

            int r = sd_bus_match_signal(bus, &ctx->discovery_slot, "org.bluez", "/",
                                        "org.freedesktop.DBus.ObjectManager", "InterfacesAdded",
                                        on_interfaces_added, dctx);
            if (r < 0) {
                std::lock_guard<std::mutex> lock(ctx->mutex);
                ctx->last_error = "failed to add InterfacesAdded match";
                setup_result = r;
                return;
            }

            sd_bus_error error = SD_BUS_ERROR_NULL;
            r = sd_bus_call_method(bus, "org.bluez", adapter.c_str(), "org.bluez.Adapter1",
                                   "StartDiscovery", &error, nullptr, "");
            if (r < 0 && !sd_bus_error_has_name(&error, "org.bluez.Error.InProgress")) {
                std::lock_guard<std::mutex> lock(ctx->mutex);
                ctx->last_error = error.message ? error.message : "StartDiscovery failed";
                sd_bus_error_free(&error);
                sd_bus_slot_unref(ctx->discovery_slot);
                ctx->discovery_slot = nullptr;
                setup_result = r;
                return;
            }
            sd_bus_error_free(&error);
        });
    } catch (const std::exception& e) {
        fprintf(stderr, "[bt] discover setup failed: %s\n", e.what());
        setup_result = -EIO;
    }

    if (setup_result < 0) {
        ctx->discovering.store(false);
        delete dctx;
        return setup_result;
    }

    fprintf(stderr, "[bt] discovery started (timeout=%dms)\n", timeout_ms);

    // Wait on the UI-side caller thread while the bus thread processes events.
    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);
    while (!stopped()) {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        long elapsed_ms =
            (now.tv_sec - start.tv_sec) * 1000 + (now.tv_nsec - start.tv_nsec) / 1000000;
        if (elapsed_ms >= timeout_ms) {
            fprintf(stderr, "[bt] discovery timeout reached (%ldms)\n", elapsed_ms);
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // Teardown on the bus thread.
    try {
        ctx->bus_thread->run_sync([&](sd_bus* bus) {
            sd_bus_error error2 = SD_BUS_ERROR_NULL;
            sd_bus_call_method(bus, "org.bluez", adapter.c_str(), "org.bluez.Adapter1",
                               "StopDiscovery", &error2, nullptr, "");
            sd_bus_error_free(&error2);
            if (ctx->discovery_slot) {
                sd_bus_slot_unref(ctx->discovery_slot);
                ctx->discovery_slot = nullptr;
            }
        });
    } catch (const std::exception& e) {
        fprintf(stderr, "[bt] discover teardown failed: %s\n", e.what());
    }

    ctx->discovering.store(false);
    delete dctx;

    fprintf(stderr, "[bt] discovery finished\n");
    return 0;
}

extern "C" void helix_bt_stop_discovery(helix_bt_context* ctx) {
    if (!ctx)
        return;
    ctx->discover_stop_gen.fetch_add(1);
    ctx->discovering.store(false);
}

extern "C" int helix_bt_enumerate_known(helix_bt_context* ctx, helix_bt_discover_cb cb,
                                        void* user_data) {
    if (!ctx || !ctx->bus_thread)
        return -EINVAL;

    discover_ctx dctx{ctx, cb, user_data};

    try {
        ctx->bus_thread->run_sync([&](sd_bus* bus) { enumerate_known_devices(bus, &dctx); });
    } catch (const std::exception& e) {
        fprintf(stderr, "[bt] enumerate_known failed: %s\n", e.what());
        return -EIO;
    }
    return 0;
}
