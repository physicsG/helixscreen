// SPDX-License-Identifier: GPL-3.0-or-later

#include "bluetooth_loader.h"

#include "runtime_config.h"

#include <spdlog/spdlog.h>

#include <climits>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <dlfcn.h>
#include <string>
#include <unistd.h>

namespace helix::bluetooth {

bool bluetooth_enabled(const char* env_value, bool test_mode) {
    if (env_value && std::strcmp(env_value, "0") == 0)
        return false;
    if (env_value && std::strcmp(env_value, "1") == 0)
        return true;
    return !test_mode;
}

BluetoothLoader& BluetoothLoader::instance() {
    static BluetoothLoader instance;
    return instance;
}

BluetoothLoader::BluetoothLoader() {
    const auto* rc = get_runtime_config();
    if (!bluetooth_enabled(::getenv("HELIX_BLUETOOTH"), rc && rc->test_mode)) {
        spdlog::info("[BluetoothLoader] Disabled (HELIX_BLUETOOTH=0, or --test without "
                     "HELIX_BLUETOOTH=1)");
        return;
    }
    if (!has_bt_hardware()) {
        spdlog::info(
            "[BluetoothLoader] No Bluetooth hardware detected (no /sys/class/bluetooth/hci*)");
        return;
    }
    spdlog::info("[BluetoothLoader] Bluetooth hardware detected");

    if (try_load()) {
        available_ = true;
        spdlog::info("[BluetoothLoader] Plugin loaded successfully");
    } else {
        spdlog::info("[BluetoothLoader] Plugin not available (expected on dev machines)");
    }
}

BluetoothLoader::~BluetoothLoader() {
    if (shared_ctx_ && deinit) {
        deinit(shared_ctx_);
        shared_ctx_ = nullptr;
    }
    // The plugin stays mapped until the process ends: a SharedContext released after this
    // destructor, by a worker or a later static, still calls the deinit it captured.
}

// Callers run on print workers and Forget workers at once, so creation is serialized: a
// second init() would register a competing BlueZ agent. A failed init() is retried by the
// next caller.
helix_bt_context* BluetoothLoader::get_or_create_context() {
    std::lock_guard<std::mutex> lock(ctx_mutex_);
    if (shared_ctx_)
        return shared_ctx_;
    if (!available_ || !init)
        return nullptr;
    shared_ctx_ = init();
    return shared_ctx_;
}

bool BluetoothLoader::is_available() const {
    return available_;
}

bool BluetoothLoader::has_bt_hardware() {
    DIR* dir = opendir("/sys/class/bluetooth");
    if (!dir)
        return false;
    bool found = false;
    while (const dirent* entry = readdir(dir)) {
        if (std::strncmp(entry->d_name, "hci", 3) == 0) {
            found = true;
            break;
        }
    }
    closedir(dir);
    return found;
}

bool BluetoothLoader::try_load() {
    // Determine path to plugin .so next to the executable
    char exe_path[PATH_MAX] = {};
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len <= 0) {
        spdlog::warn("[BluetoothLoader] Cannot determine executable path");
        return false;
    }
    exe_path[len] = '\0';

    // Find directory containing the executable
    std::string dir(exe_path);
    auto slash = dir.rfind('/');
    if (slash != std::string::npos) {
        dir = dir.substr(0, slash + 1);
    } else {
        dir = "./";
    }

    std::string plugin_path = dir + "libhelix-bluetooth.so";
    spdlog::debug("[BluetoothLoader] Trying to load: {}", plugin_path);

    dl_handle_ = dlopen(plugin_path.c_str(), RTLD_NOW);
    if (!dl_handle_) {
        spdlog::debug("[BluetoothLoader] dlopen failed: {}", dlerror());
        return false;
    }

    // Resolve get_info first to verify API version
    auto get_info =
        reinterpret_cast<helix_bt_get_info_fn>(dlsym(dl_handle_, HELIX_BT_SYM_GET_INFO));
    if (!get_info) {
        spdlog::warn("[BluetoothLoader] Missing symbol: {}", HELIX_BT_SYM_GET_INFO);
        dlclose(dl_handle_);
        dl_handle_ = nullptr;
        return false;
    }

    auto* info = get_info();
    if (!info || info->api_version != HELIX_BT_API_VERSION) {
        spdlog::warn("[BluetoothLoader] API version mismatch: expected {}, got {}",
                     HELIX_BT_API_VERSION, info ? info->api_version : -1);
        dlclose(dl_handle_);
        dl_handle_ = nullptr;
        return false;
    }

    spdlog::debug("[BluetoothLoader] Plugin '{}' v{}, classic={}, ble={}",
                  info->name ? info->name : "unknown", info->api_version, info->has_classic,
                  info->has_ble);

    // Resolve all function pointers
    auto resolve = [this](const char* name) -> void* {
        void* sym = dlsym(dl_handle_, name);
        if (!sym) {
            spdlog::warn("[BluetoothLoader] Missing symbol: {}", name);
        }
        return sym;
    };

    init = reinterpret_cast<helix_bt_init_fn>(resolve(HELIX_BT_SYM_INIT));
    deinit = reinterpret_cast<helix_bt_deinit_fn>(resolve(HELIX_BT_SYM_DEINIT));
    discover = reinterpret_cast<helix_bt_discover_fn>(resolve(HELIX_BT_SYM_DISCOVER));
    stop_discovery =
        reinterpret_cast<helix_bt_stop_discovery_fn>(resolve(HELIX_BT_SYM_STOP_DISCOVERY));
    enumerate_known =
        reinterpret_cast<helix_bt_enumerate_known_fn>(resolve(HELIX_BT_SYM_ENUMERATE_KNOWN));
    pair = reinterpret_cast<helix_bt_pair_fn>(resolve(HELIX_BT_SYM_PAIR));
    is_paired = reinterpret_cast<helix_bt_is_paired_fn>(resolve(HELIX_BT_SYM_IS_PAIRED));
    is_bonded = reinterpret_cast<helix_bt_is_bonded_fn>(resolve(HELIX_BT_SYM_IS_BONDED));
    is_connected = reinterpret_cast<helix_bt_is_connected_fn>(resolve(HELIX_BT_SYM_IS_CONNECTED));
    remove_device =
        reinterpret_cast<helix_bt_remove_device_fn>(resolve(HELIX_BT_SYM_REMOVE_DEVICE));
    connect_rfcomm =
        reinterpret_cast<helix_bt_connect_rfcomm_fn>(resolve(HELIX_BT_SYM_CONNECT_RFCOMM));
    sdp_find_rfcomm_channel = reinterpret_cast<helix_bt_sdp_find_rfcomm_channel_fn>(
        resolve(HELIX_BT_SYM_SDP_FIND_RFCOMM_CHANNEL));
    connect_ble = reinterpret_cast<helix_bt_connect_ble_fn>(resolve(HELIX_BT_SYM_CONNECT_BLE));
    ble_write = reinterpret_cast<helix_bt_ble_write_fn>(resolve(HELIX_BT_SYM_BLE_WRITE));
    ble_read = reinterpret_cast<helix_bt_ble_read_fn>(resolve(HELIX_BT_SYM_BLE_READ));
    disconnect = reinterpret_cast<helix_bt_disconnect_fn>(resolve(HELIX_BT_SYM_DISCONNECT));
    last_error = reinterpret_cast<helix_bt_last_error_fn>(resolve(HELIX_BT_SYM_LAST_ERROR));
    lzo_compress = reinterpret_cast<helix_bt_lzo_compress_fn>(resolve(HELIX_BT_SYM_LZO_COMPRESS));

    // Verify required symbols loaded
    if (!init || !deinit) {
        spdlog::warn("[BluetoothLoader] Required symbols (init/deinit) not resolved");
        dlclose(dl_handle_);
        dl_handle_ = nullptr;
        return false;
    }

    return true;
}

} // namespace helix::bluetooth
