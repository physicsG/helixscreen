// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "async_lifetime_guard.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace helix::plugin {

/// One file under config/helixscreen/plugins/ as Moonraker lists it.
struct RemoteFile {
    std::string path; ///< relative to the plugin root: "<id>/main.lua", "<id>/ui/x.xml"
    uint64_t size = 0;
    double modified = 0.0;
};

struct SourceDeps {
    /// Lists every file under config/helixscreen/plugins/. `ok` false means the listing
    /// failed (disconnected, Moonraker error): nothing may be deleted on a failed listing.
    /// An absent folder is ok with an empty vector.
    std::function<void(std::function<void(bool ok, std::vector<RemoteFile>)>)> list;
    /// Downloads one file (path relative to the plugin root) to `dest`, writing at
    /// most `max_bytes`: a file that would exceed it fails the download. The cap is
    /// min(per-file limit, the plugin's remaining byte budget) + 1, so a file that
    /// exactly fits the limit passes and one byte more does not.
    std::function<void(const std::string& path, const std::string& dest, size_t max_bytes,
                       std::function<void(bool ok, std::string error)>)>
        download;
};

struct SyncResult {
    std::vector<std::string> changed;  ///< ids whose cache content changed (new or updated)
    std::vector<std::string> removed;  ///< ids whose source is gone; their cache dir is deleted
    std::vector<std::string> failed;   ///< ids left at their previous cached version
    std::vector<std::string> rejected; ///< ids refused by a limit, with the reason logged
};

/// Per-plugin limits. A plugin over any of them is skipped whole (reported as rejected);
/// its previous cached version, if any, stays.
constexpr size_t kMaxFilesPerPlugin = 128;
constexpr uint64_t kMaxBytesPerPlugin = 8ull << 20;
constexpr uint64_t kMaxBytesPerFile = 4ull << 20;
constexpr size_t kMaxPlugins = 32;

/// Mirrors the Moonraker plugin root into a per-printer cache directory. Main thread
/// only; the deps may complete on any thread, and every completion hops back through
/// the lifetime token before touching this object's state.
class PluginSource {
  public:
    /// `cache_dir` is the per-printer cache root; it and its `.index.json` are created on
    /// the first sync.
    PluginSource(SourceDeps deps, std::string cache_dir);
    ~PluginSource();
    PluginSource(const PluginSource&) = delete;
    PluginSource& operator=(const PluginSource&) = delete;

    /// Lists, diffs against the cache index, downloads what changed into
    /// `<cache>/.staging/<id>/`, swaps each fully downloaded plugin into place, deletes
    /// plugins whose source is gone, and calls `done` once on the main thread. A second
    /// sync requested while one runs is queued and runs once after it (never in parallel).
    void sync(std::function<void(const SyncResult&)> done);

    bool syncing() const {
        return syncing_;
    }
    const std::string& cache_dir() const {
        return cache_dir_;
    }

  private:
    /// (size, modified) as the index records one file.
    using FileEntry = std::pair<uint64_t, double>;
    /// rest-path -> entry, one plugin's file set.
    using PluginFiles = std::map<std::string, FileEntry>;
    /// id -> file set: the shape of `<cache>/.index.json`.
    using CacheIndex = std::map<std::string, PluginFiles>;
    /// One queued download.
    struct Pending {
        std::string id;
        std::string rest;
        uint64_t size = 0;
        double modified = 0.0;
    };
    using DoneList = std::vector<std::function<void(const SyncResult&)>>;

    /// Begins one sync for the given callers: resets the per-sync state, cleans leftover
    /// `.old-*` and `.staging` directories and issues the listing.
    void start(DoneList done);
    /// Runs `fn` on the main thread: inline when the dep answered there (`this` is then
    /// mid-call, alive by construction), through the token's queue hop when it answered
    /// on a worker thread. Static because a worker may call it while `this`, destroyed
    /// on the main thread mid-transfer, no longer exists; the token alone decides.
    static void hop(const LifetimeToken& tok, const char* tag, std::function<void()> fn);
    void on_listed(bool ok, const std::vector<RemoteFile>& files);
    /// True when the plugin's file count and sizes fit the per-plugin limits; the
    /// plugin-count limit is applied by the caller, which sees the accepted ids.
    bool within_limits(const std::string& id, const PluginFiles& plugin_files) const;
    /// Issues the next queued download, or completes the sync when none remain. Files of
    /// a plugin already marked failed are skipped without a request.
    void download_next();
    void on_downloaded(size_t index, bool ok, const std::string& error);
    /// Fails the whole id: drops its staged files and staging directory, keeps its
    /// cached version and index entry untouched, and reports it once in `failed`.
    void fail_id(const std::string& id, const std::string& why);
    /// The byte cap handed to the next download of `pending`: the smaller of the
    /// per-file limit and the plugin's remaining byte budget, plus one.
    size_t byte_cap(const Pending& pending) const;
    /// Swaps every fully downloaded plugin in (all in one main-thread step, so no plugin
    /// runs Lua against a half-updated cache), removes plugins gone from the source,
    /// writes the index and finishes.
    void complete();
    /// Delivers `result`, then runs the one queued sync with its collected callers.
    void finish(SyncResult result);
    /// Puts back `<cache>/.old-<id>` when a crash between the two renames of a swap
    /// left the plugin nowhere else, then removes `<cache>/.staging` and every
    /// remaining `<cache>/.old-*`.
    void clean_leftovers();
    CacheIndex load_index() const;
    bool save_index(const CacheIndex& index) const;
    std::filesystem::path plugin_dir(const std::string& id) const;
    std::filesystem::path staging_dir(const std::string& id) const;
    std::filesystem::path old_dir(const std::string& id) const;
    std::filesystem::path index_path() const;

    SourceDeps deps_;
    std::string cache_dir_;
    AsyncLifetimeGuard guard_;
    bool syncing_ = false;
    DoneList done_;   ///< callers waiting on the running sync
    DoneList queued_; ///< callers waiting on the one queued run

    // State of the running sync; meaningful only while syncing_.
    CacheIndex index_;
    CacheIndex staged_;                     ///< fully downloaded plugins awaiting the swap
    std::map<std::string, uint64_t> spent_; ///< bytes written per id this sync
    std::set<std::string> failed_;
    std::set<std::string> seen_; ///< ids present in the listing (rejected ones included)
    std::vector<Pending> queue_;
    size_t queue_pos_ = 0;
    SyncResult result_;
};

/// Splits "<id>/<rest>" and checks both halves: the id matches the plugin id pattern and
/// the rest is a relative path with no "..", no leading "/", no backslash, no empty
/// segment and no NUL. Returns false for anything else. Pure.
bool split_plugin_file(const std::string& path, std::string& id, std::string& rest);

} // namespace helix::plugin
