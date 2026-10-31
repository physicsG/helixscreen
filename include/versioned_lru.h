// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "file_version.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <utility>

namespace helix {

/// Values derived from a printer file's content, keyed by the file and kept
/// for the version they were read from (a FileVersion unless @p Version says
/// otherwise), bounded by least recent use.
template <typename T, typename Version = FileVersion> class VersionedLru {
  public:
    struct Entry {
        Version version;
        T value{};
        uint64_t last_used = 0; ///< higher = more recent
    };

    /// The entry under @p key, whatever its version. Marks nothing used.
    [[nodiscard]] const Entry* find(const std::string& key) const {
        const auto it = entries_.find(key);
        return it == entries_.end() ? nullptr : &it->second;
    }

    /// The value under @p key when it was kept for @p version, marked used;
    /// null on a miss or a value kept for another version.
    [[nodiscard]] T* lookup(const std::string& key, const Version& version) {
        const auto it = entries_.find(key);
        if (it == entries_.end() || it->second.version != version) {
            return nullptr;
        }
        touch(it->second);
        return &it->second.value;
    }

    /// Keep @p value for @p version under @p key, marked used.
    void put(const std::string& key, const Version& version, T value) {
        Entry& e = entries_[key];
        e.version = version;
        e.value = std::move(value);
        touch(e);
    }

    /// Mark the entry under @p key used, if there is one.
    void mark_used(const std::string& key) {
        if (const auto it = entries_.find(key); it != entries_.end()) {
            touch(it->second);
        }
    }

    void erase(const std::string& key) {
        entries_.erase(key);
    }

    void clear() {
        entries_.clear();
    }

    [[nodiscard]] size_t size() const {
        return entries_.size();
    }

    /// Drop the least recently used entries until at most @p max remain,
    /// never one @p pinned names. Stops early when only pinned entries are left.
    template <typename Pinned> void evict(size_t max, Pinned pinned) {
        while (entries_.size() > max) {
            auto oldest = entries_.end();
            for (auto it = entries_.begin(); it != entries_.end(); ++it) {
                if (!pinned(it->first) &&
                    (oldest == entries_.end() || it->second.last_used < oldest->second.last_used)) {
                    oldest = it;
                }
            }
            if (oldest == entries_.end()) {
                return;
            }
            entries_.erase(oldest);
        }
    }

    void evict(size_t max) {
        evict(max, [](const std::string&) { return false; });
    }

    /// Every entry, by key, for persisting.
    [[nodiscard]] const std::map<std::string, Entry>& entries() const {
        return entries_;
    }

  private:
    void touch(Entry& e) {
        e.last_used = ++use_counter_;
    }

    std::map<std::string, Entry> entries_;
    uint64_t use_counter_ = 0;
};

} // namespace helix
