// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>

namespace helix {

/// Snapshot @p path into a fixed static buffer so the SIGTERM handler can
/// unlink(2) the crash-restart marker without constructing anything. Must be
/// called on the main thread at startup, before the handler can fire. A path
/// that does not fit is rejected (the handler then does nothing).
/// @return true if the path was cached.
bool cache_crash_marker_path_for_signal(const std::string& path);

/// Delete the crash-restart marker using only async-signal-safe calls.
/// Callable from a signal handler: no allocation, no std::filesystem, no
/// locking — just unlink(2) on the pre-cached path. A no-op when the path was
/// never cached.
void clear_crash_marker_signal_safe();

/// Path of the crash-restart marker, in the writable config dir.
const std::string& crash_marker_path();

/// Record a start at @p now_epoch (seconds) in the marker file at @p marker_path and
/// report whether enough recent starts accumulated to be a crash loop. A loop
/// removes the marker, sets RuntimeConfig::crash_loop_safe_mode for this run and
/// returns true; otherwise the start is appended.
bool record_start_and_check_crash_loop(const std::string& marker_path, long long now_epoch);

/// record_start_and_check_crash_loop() against the real marker and clock. A no-op
/// in test mode, where automation relaunches the binary rapidly by design.
void crash_loop_detected_and_record();

/// Promote any surviving GPU 3D / GPU blur crash-loop guard file to a persistent
/// block in config, then remove the guard so a later clean run can re-arm it.
void promote_surviving_gpu_guards();

/// Read and delete the watchdog's Safe Mode marker (primary and /tmp fallback
/// paths). @return true if either was present.
bool consume_safe_mode_marker();

} // namespace helix
