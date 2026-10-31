// Copyright (C) 2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <optional>
#include <string>

/**
 * @file wifi_regdomain.h
 * @brief The kernel's regulatory domain, read and set through `iw`, shared by
 *        the Linux WiFi backends. The parsing lives in wifi_country.h.
 *
 * Everything here is BLOCKING (a subprocess or a /proc read): call it from
 * WiFiManager's control lane or a backend's own thread, never the LVGL thread.
 */
namespace helix::wifi::regdomain {

/// The `iw` binary, or nullptr when none is installed.
const char* find_iw();

/// Runs `<iw> <args>` and returns its stdout. @p args is a fixed verb plus at
/// most a validated two-letter code, so the shell sees nothing a caller typed.
std::string exec_iw(const char* iw, const std::string& args, int& exit_code);

/// The global country `iw reg get` reports: "" for the world domain, nullopt
/// when there is no `iw` or it failed.
std::optional<std::string> read_country();

/// Whether this process may change the regulatory domain: `iw reg set` needs
/// root or CAP_NET_ADMIN, and the kernel refuses it otherwise.
bool process_may_set();

/// Test seam: answer find_iw() with @p path ("" = no iw installed) until
/// reset_iw_for_test().
void set_iw_for_test(const std::string& path);
void reset_iw_for_test();

} // namespace helix::wifi::regdomain
