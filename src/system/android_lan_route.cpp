// SPDX-License-Identifier: GPL-3.0-or-later

// Routes the app's LAN sockets over the connected Wi-Fi.
//
// With mobile data up, Android keeps a Wi-Fi network that has no internet off
// the default route, so a socket to a 192.168.x.x printer leaves over cellular
// and times out. Binding the whole process to Wi-Fi would fix that and break
// everything that needs the internet (update checks, Play, telemetry, crash
// uploads, a printer reached remotely). So only sockets whose peer is on the LAN
// are bound, one at a time, from libhv's outbound socket hook; everything else
// keeps the system's default route.
//
// WifiBinder (Java) publishes the Wi-Fi network handle, or 0 when no Wi-Fi is
// connected or a VPN is the default network (a VPN may carry the route to that
// LAN, e.g. a Tailscale subnet router).

#ifdef __ANDROID__

#include "hv/hsocket.h"
#include "utils/lan_host.h"

#include <spdlog/spdlog.h>

#include <android/multinetwork.h>
#include <atomic>
#include <cerrno>
#include <cstring>
#include <jni.h>

namespace {

std::atomic<int64_t> g_wifi_handle{0};

void bind_lan_socket(int fd, const struct sockaddr* peer) {
    const int64_t handle = g_wifi_handle.load(std::memory_order_acquire);
    if (handle == 0 || !helix::net::is_lan_address(peer)) {
        return;
    }
    if (android_setsocknetwork(static_cast<net_handle_t>(handle), fd) != 0) {
        spdlog::warn("[LanRoute] Binding fd {} to Wi-Fi failed: {}", fd, std::strerror(errno));
        return;
    }
    spdlog::debug("[LanRoute] fd {} bound to Wi-Fi network {}", fd, handle);
}

} // namespace

// NAMESPACE_OK: JNI resolves this by its global C name
extern "C" JNIEXPORT void JNICALL Java_org_helixscreen_app_WifiBinder_nativeSetWifiNetwork(
    JNIEnv* /*env*/, jclass /*cls*/, jlong handle) {
    static std::atomic<bool> hook_installed{false};
    if (!hook_installed.exchange(true)) {
        hv_set_outbound_socket_hook(&bind_lan_socket);
    }
    const int64_t previous = g_wifi_handle.exchange(handle, std::memory_order_acq_rel);
    if (previous != handle) {
        spdlog::info("[LanRoute] LAN sockets now use {}",
                     handle != 0 ? "Wi-Fi network " + std::to_string(handle)
                                 : std::string("the default network"));
    }
}

#endif // __ANDROID__
