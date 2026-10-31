// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "file_version.h"
#include "versioned_lru.h"

#include <cstdint>
#include <ctime>
#include <optional>
#include <set>
#include <string>

namespace helix {

/// Persistent per-file cache of the tools-used set recovered from a gcode
/// scan/viewer parse. Keyed by (path, size, mtime) so re-sliced files
/// invalidate naturally. JSON map in <cache>/tools_used/cache.json,
/// LRU-bounded. Pure logic — no LVGL, safe on any thread (internally
/// serialized with a mutex if a mutex already exists; single-threaded use
/// from the detail view today, so keep it simple: document main-thread use).
class ToolsUsedCache {
  public:
    ToolsUsedCache() = default;

    /// nullopt = miss / stale / malformed. An EMPTY set is a legitimate
    /// cached value (single-extruder file) and is returned as such.
    std::optional<std::set<int>> lookup(const std::string& file_path, FileVersion version);

    /// Persist an entry (also compacts when over MAX_ENTRIES — drop
    /// least-recently-LOOKED-UP first). Writes through to disk immediately;
    /// a failed write logs a warning and keeps the in-memory entry.
    void store(const std::string& file_path, FileVersion version, const std::set<int>& tools);

    static constexpr size_t MAX_ENTRIES = 256;

    /// Drop the in-memory copy; the next use reads the file again. Not needed
    /// in production flow: tests reset the shared instance with it when they
    /// point the cache directory elsewhere.
    void invalidate() {
        entries_.clear();
        loaded_ = false;
    }

  private:
    bool load_from_disk();
    void save_to_disk();

    VersionedLru<std::set<int>> entries_;
    bool loaded_ = false;
};

/// The one cache every reader and writer shares: the file view's chips and a
/// queued start's facts. One instance, because each saves its whole map, so a
/// second would drop the first's entries and lose its lookup recency.
ToolsUsedCache& tools_used_cache();

} // namespace helix
