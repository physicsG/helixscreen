// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#if HELIX_HAS_PLUGINS

#include "plugin_host.h"

#include <cstdint>
#include <lvgl.h>
#include <map>
#include <string>

namespace helix::plugin {

/// A cheap fingerprint of one plugin directory: file count, total size and the
/// newest mtime of every regular file under it. Pure over the filesystem.
struct DirSignature {
    size_t files = 0;
    uint64_t bytes = 0;
    int64_t newest_mtime_ns = 0;
    bool operator==(const DirSignature& other) const {
        return files == other.files && bytes == other.bytes &&
               newest_mtime_ns == other.newest_mtime_ns;
    }
};

/// One signature per valid plugin id directly under `dir`. Names that fail the
/// plugin id check (dot-directories, stray files) are skipped, and no file
/// content is read, so a poll costs only stat calls.
std::map<std::string, DirSignature> scan_plugin_dir(const std::string& dir);

/// Polls `dir` every `interval_ms` on an lv_timer and calls host.rescan() with
/// the ids whose signature changed, appeared or disappeared since the last
/// poll. The constructor takes the first scan as the baseline, so a watcher
/// built right after load_from() reloads nothing until a file changes. Main
/// thread only: the timer fires from lv_timer_handler.
class PluginDirWatcher {
  public:
    PluginDirWatcher(PluginHost& host, std::string dir, uint32_t interval_ms = 1000);
    ~PluginDirWatcher();
    PluginDirWatcher(const PluginDirWatcher&) = delete;
    PluginDirWatcher& operator=(const PluginDirWatcher&) = delete;

    void poll_now(); ///< one poll, for tests

  private:
    static void on_timer(lv_timer_t* timer);
    void stop();

    PluginHost& host_;
    std::string dir_;
    std::map<std::string, DirSignature> last_;
    lv_timer_t* timer_ = nullptr;
};

} // namespace helix::plugin

#endif // HELIX_HAS_PLUGINS
