// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The board's wall clock. Nothing persists it across power-off, so time()
// counts from 1970 until something sets it: SNTP when the network reaches the
// internet, else the Date header of a Moonraker HTTP response (LAN-only
// printers). SNTP keeps correcting the clock whenever it answers.

#pragma once

#include <string>

namespace helix::wall_clock_esp {

// Starts SNTP and listens for HTTP Date headers. Call once, after esp_netif_init().
void start();

// Asks Moonraker for one small response so its Date header can set a clock
// that SNTP has not. No-op once the clock is set.
void request_date(const std::string& http_base_url);

} // namespace helix::wall_clock_esp
