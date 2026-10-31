// Copyright (C) 2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>

namespace helix {

/// How a local power request ended.
enum class PowerResult {
    Dispatched,    ///< One of the mechanisms accepted the request.
    NotAuthorized, ///< logind refused for lack of a polkit grant, and every fallback failed.
    Failed,        ///< Every mechanism failed for another reason.
};

/// True when @p dbus_error_name is logind's answer to a caller polkit has not
/// authorized. Pure; null reads as false.
bool is_authorization_error(const char* dbus_error_name);

/// True when systemd is the running init under @p root ("" = this host),
/// read from the /run/systemd/system marker the way sd_booted() reads it. A
/// systemctl binary proves nothing: chroots, containers and SysV boxes ship one.
bool host_runs_systemd(const std::string& root = {});

/// Initiates shutdown/reboot of the machine running helixscreen. Tries logind
/// via sd-bus first; falls back to systemctl, then busybox, if sd-bus is
/// unavailable or the call fails.
class SystemPower {
  public:
    static PowerResult reboot_local();
    static PowerResult shutdown_local();
};

} // namespace helix
