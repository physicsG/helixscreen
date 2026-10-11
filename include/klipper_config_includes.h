// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

class IMoonrakerAPI;
struct MoonrakerError;

namespace helix::system {

// ============================================================================
// Pure path/glob utilities (extracted from KlipperConfigEditor)
// ============================================================================

/// Get the directory portion of a file path (everything before the last '/')
[[nodiscard]] std::string config_get_directory(const std::string& path);

/// Resolve a relative include path against the directory of the including file
[[nodiscard]] std::string config_resolve_path(const std::string& current_file,
                                              const std::string& include_path);

/// Glob match mirroring Python glob(recursive=True): `*` and `?` stay within one path
/// segment, `**` spans separators. The one glob for include resolution and ctl widget names.
[[nodiscard]] bool config_glob_match(const std::string& pattern, const std::string& text);

/// Find all files in the map that match a glob pattern (resolved relative to current file)
[[nodiscard]] std::vector<std::string>
config_match_glob(const std::map<std::string, std::string>& files, const std::string& current_file,
                  const std::string& include_pattern);

// ============================================================================
// Include resolution
// ============================================================================

/// Extract [include ...] directives from config file content.
/// Returns a list of include paths/patterns (e.g., "macros.cfg", "conf.d/*.cfg").
[[nodiscard]] std::vector<std::string> extract_includes(const std::string& content);

/// A byte range [begin, end) of one config file. Klipper reads the active config
/// as a sequence of these: a file up to an [include] line, everything that include
/// reads, then the rest of the file. A later section overrides an earlier one.
struct ConfigSegment {
    std::string file;
    size_t begin = 0;
    size_t end = 0;
};

/// Walk the include chain from root_file and return the set of active file paths.
/// Pure function: given a map of filename->content, follows [include ...] directives
/// recursively, handling globs and cycle detection.
/// @param files Map of filename -> content (all files in config directory)
/// @param root_file Starting file (usually "printer.cfg")
/// @param max_depth Maximum recursion depth (default 5)
/// @param read_order If set, receives the active config in Klipper's read order
/// @return Set of file paths that are part of the active include chain
[[nodiscard]] std::set<std::string>
resolve_active_files(const std::map<std::string, std::string>& files, const std::string& root_file,
                     int max_depth = 5, std::vector<ConfigSegment>* read_order = nullptr);

// ============================================================================
// Async Moonraker integration
// ============================================================================

using ActiveFilesCallback = std::function<void(const std::set<std::string>&)>;
using ErrorCallback = std::function<void(const std::string&)>;

/// Callback providing active files AND their downloaded contents
using ActiveFilesWithContentCallback =
    std::function<void(const std::set<std::string>&, const std::map<std::string, std::string>&)>;

/// Downloads one config file and calls exactly one of @p on_ok (content) or
/// @p on_fail (message, and whether the transport refused it for a full queue).
/// Either may run before the call returns.
using ConfigDownloadFn =
    std::function<void(const std::string& path, std::function<void(std::string)> on_ok,
                       std::function<void(std::string, bool queue_full)> on_fail)>;

/// Config downloads outstanding at once. The ESP32 HTTP lane queues 8 requests
/// for every caller, thumbnails included, and frees a slot only after the
/// completion callback returns, so the walk can hold one more than this.
inline constexpr size_t kMaxConfigDownloadsInFlight = 4;

/// Download @p root_file and every file its [include] chain reaches, following
/// globs against @p listing (every path in the config root) and paths relative
/// to the including file. Files outside the chain are never fetched. A download
/// refused for a full queue is retried when an in-flight one completes; with
/// nothing in flight, and for any other failure, the whole walk fails through
/// @p on_error once the outstanding downloads have returned. A partial set would
/// read as a config without the missing files.
void download_include_graph(std::vector<std::string> listing, const std::string& root_file,
                            ConfigDownloadFn download, ActiveFilesWithContentCallback on_complete,
                            ErrorCallback on_error,
                            size_t max_in_flight = kMaxConfigDownloadsInFlight, int max_depth = 5);

/// Async wrapper: lists config directory via Moonraker, downloads printer.cfg and
/// all included files, then resolves the active file set.
/// Handles glob includes by cross-referencing the full file listing — the
/// pattern names files that are unknowable until Moonraker lists the directory.
void resolve_active_config_files(IMoonrakerAPI& api, ActiveFilesCallback on_complete,
                                 ErrorCallback on_error);

/// Async wrapper that also returns file contents for the active files.
/// Identical to resolve_active_config_files() but the callback also receives
/// the map of filename -> content for every file in the include chain.
void resolve_active_config_files_with_content(IMoonrakerAPI& api,
                                              ActiveFilesWithContentCallback on_complete,
                                              ErrorCallback on_error);

} // namespace helix::system
