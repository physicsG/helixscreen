// Copyright (C) 2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "system_power.h"

#include <spdlog/spdlog.h>

#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <sys/wait.h>

#ifdef HELIX_HAS_SYSTEMD
#include <systemd/sd-bus.h>
#endif

namespace helix {

bool is_authorization_error(const char* dbus_error_name) {
    if (!dbus_error_name) {
        return false;
    }
    return std::strcmp(dbus_error_name,
                       "org.freedesktop.DBus.Error.InteractiveAuthorizationRequired") == 0 ||
           std::strcmp(dbus_error_name, "org.freedesktop.DBus.Error.AccessDenied") == 0;
}

bool host_runs_systemd(const std::string& root) {
    struct stat st {};
    return stat((root + "/run/systemd/system").c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

namespace {

#ifdef HELIX_HAS_SYSTEMD
// Returns Dispatched, NotAuthorized (polkit refused this user) or Failed.
PowerResult logind_call(const char* method) {
    sd_bus* bus = nullptr;
    const int open_rc = sd_bus_open_system(&bus);
    if (open_rc < 0 || !bus) {
        spdlog::warn("[SystemPower] sd_bus_open_system failed: {}", std::strerror(-open_rc));
        return PowerResult::Failed;
    }

    sd_bus_error err = SD_BUS_ERROR_NULL;
    sd_bus_message* reply = nullptr;
    const int r = sd_bus_call_method(bus, "org.freedesktop.login1", "/org/freedesktop/login1",
                                     "org.freedesktop.login1.Manager", method, &err, &reply, "b",
                                     0 /* interactive=false */);
    PowerResult result = PowerResult::Dispatched;
    if (r < 0) {
        spdlog::warn("[SystemPower] logind {} failed: {} ({})", method,
                     err.message ? err.message : "no message", err.name ? err.name : "");
        result =
            is_authorization_error(err.name) ? PowerResult::NotAuthorized : PowerResult::Failed;
        if (result == PowerResult::NotAuthorized) {
            spdlog::warn("[SystemPower] No polkit rule grants this user {}; restarting the "
                         "service installs one (config/ensure-polkit-rule.sh)",
                         method);
        }
    }
    sd_bus_error_free(&err);
    if (reply)
        sd_bus_message_unref(reply);
    sd_bus_unref(bus);
    return result;
}
#endif

bool exec_fallback(const char* desc, const char* cmd) {
    spdlog::info("[SystemPower] fallback: {}", desc);
    const int rc = std::system(cmd);
    if (rc == -1) {
        spdlog::warn("[SystemPower] {} fork/exec failed", desc);
        return false;
    }
    return WIFEXITED(rc) && WEXITSTATUS(rc) == 0;
}

// Non-systemd hosts (OpenWrt/procd K2, K1C, AD5M, SonicPad…) don't have
// systemctl. They do have busybox /sbin/reboot and /sbin/poweroff via
// procd's sysinit hook (`::shutdown:/etc/init.d/rcS K shutdown`).
bool busybox_fallback(const char* path) {
    return exec_fallback(path, path);
}

// logind's verdict, then systemctl, then busybox. A logind refusal for lack
// of authorization is what gets reported when the fallbacks fail too: on a
// systemd host they go through logind and meet the same polkit answer.
// systemctl is skipped where systemd is not init: inside a chroot it prints
// "Running in chroot, ignoring request" and exits 0, which would report a
// power-off that never happens.
PowerResult power_local(const char* logind_method, const char* systemctl_cmd,
                        const char* busybox_path) {
    PowerResult logind = PowerResult::Failed;
#ifdef HELIX_HAS_SYSTEMD
    logind = logind_call(logind_method);
    if (logind == PowerResult::Dispatched)
        return logind;
#else
    (void)logind_method;
#endif
    if (host_runs_systemd() && exec_fallback(systemctl_cmd, systemctl_cmd))
        return PowerResult::Dispatched;
    if (busybox_fallback(busybox_path))
        return PowerResult::Dispatched;
    return logind;
}

} // namespace

PowerResult SystemPower::reboot_local() {
    spdlog::info("[SystemPower] reboot_local");
    return power_local("Reboot", "systemctl reboot", "/sbin/reboot");
}

PowerResult SystemPower::shutdown_local() {
    spdlog::info("[SystemPower] shutdown_local");
    return power_local("PowerOff", "systemctl poweroff", "/sbin/poweroff");
}

} // namespace helix
