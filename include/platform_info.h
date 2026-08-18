// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>

namespace helix {

/// Returns true when running on Android (compile-time on real builds, overridable for tests)
bool is_android_platform();

/// True when this platform offers the host power controls (shutdown/reboot
/// dialog, screen power actions). Android is an app on someone's tablet: there
/// is no init system to reboot and the host-power RPCs are not wanted there.
/// The single source of this rule — the platform_host_power_supported subject
/// is seeded from it and the shutdown widget/dialog gate on that.
bool platform_host_power_supported();

/// Test helper: override the platform check. Pass -1 to reset to compile-time default.
void set_platform_override(int override_value);

/// Log platform info (kernel, arch, hostname, memory) at INFO level
void log_platform_info();

/// Short human-readable host arch line for the About screen and debug bundles.
/// Format: "<kernel-arch> · <N>-bit userspace" — surfaces the common
/// "aarch64 kernel + 32-bit userspace" Pi configuration without coercing the
/// user to migrate.
std::string host_arch_string();

} // namespace helix
