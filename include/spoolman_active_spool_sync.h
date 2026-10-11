// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "i_moonraker_sub_apis.h"
#include "spoolman_types.h"

#include <string>

#include "hv/json.hpp"

namespace helix {

class IMoonrakerClient;

/**
 * @file spoolman_active_spool_sync.h
 * @brief Mirrors Moonraker's global active Spoolman spool onto the external-spool
 *        (bypass) slot and onto the active toolchanger tool.
 *
 * Moonraker has one active spool, and an AMS lane assignment sets it too, so the
 * mirror consults AmsState::active_spool_describes_bypass() before it writes the
 * bypass record. The toolchanger auto-assign is a separate concern (which spool is
 * on the active TOOL) and runs ahead of that gate.
 */
namespace spoolman_sync {

/// Subscribe to notify_active_spool_set and sync once from Moonraker's current
/// active spool. Idempotent: every discovery calls it, and a repeat replaces the
/// earlier subscription. @p spoolman must outlive the subscription, so detach
/// before the API goes.
void attach(IMoonrakerClient& client, ISpoolmanAPI& spoolman);

/// Drop the subscription attach() made. Safe when never attached.
void detach(IMoonrakerClient& client);

/// The startup sync, given Moonraker's Spoolman status. No-op when Spoolman is down,
/// no spool is active, or the external slot already holds that spool; otherwise
/// fetches the spool and syncs it.
void sync_from_status(ISpoolmanAPI& spoolman, bool connected, int active_spool_id);

/// The notify_active_spool_set handler. @p msg is the full JSON-RPC message.
void on_active_spool_set(ISpoolmanAPI& spoolman, const nlohmann::json& msg);

/// Adopt @p spool for the active toolchanger tool when that tool has no spool
/// recorded. Main thread only.
void try_assign_active_spool_to_tool(const SpoolInfo& spool);

/// Queue @p spool onto the UI thread: tool auto-assign first, then the external
/// slot when the bypass owns the active spool. @p log_context ends the log line.
void sync_external_spool(const SpoolInfo& spool, std::string log_context);

} // namespace spoolman_sync
} // namespace helix
