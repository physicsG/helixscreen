// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/**
 * @file archive_transfers.h
 * @brief A mock transfer API serving archives held in memory, counting the
 *        ranged reads and bytes a reader asks for, and the MoonrakerAPIMock
 *        that serves it.
 */

#include "moonraker_api_mock.h"
#include "moonraker_client_mock.h"
#include "printer_state.h"

#include <fstream>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

/// Serves the files put() into it, synchronously; anything else falls through
/// to the plain mock's test files.
class ArchiveTransfers : public MoonrakerFileTransferAPIMock {
  public:
    using MoonrakerFileTransferAPIMock::MoonrakerFileTransferAPIMock;

    void put(const std::string& path, std::string bytes) {
        files_[path] = std::move(bytes);
    }

    bool supports_range_reads() const override {
        return ranged;
    }

    void download_file_range(const std::string& root, const std::string& path, uint64_t offset,
                             size_t len, StringCallback on_success, ErrorCallback on_error,
                             CancelFlag cancelled = nullptr) override {
        auto it = files_.find(path);
        if (it == files_.end()) {
            MoonrakerFileTransferAPIMock::download_file_range(
                root, path, offset, len, std::move(on_success), std::move(on_error),
                std::move(cancelled));
            return;
        }
        ++ranges;
        last_cancel = cancelled;
        range_requests.push_back({offset, len});
        if (fail_ranges || (fail_from > 0 && ranges >= fail_from)) {
            on_error(MoonrakerError::file_not_found("download_file_range", "failed by test"));
            return;
        }
        const std::string& f = it->second;
        std::string out = offset < f.size() ? f.substr(static_cast<size_t>(offset), len) : "";
        range_bytes += out.size();
        if (hold_from > 0 && ranges >= hold_from) {
            held.push_back([out = std::move(out), on_success, on_error](bool fail) {
                if (fail) {
                    on_error(
                        MoonrakerError::file_not_found("download_file_range", "failed by test"));
                } else {
                    on_success(out);
                }
            });
            return;
        }
        on_success(out);
    }

    /// Answers every held ranged read, in order: with its bytes, or as failed.
    void release_held(bool fail = false) {
        auto reads = std::move(held);
        held.clear();
        for (auto& read : reads) {
            read(fail);
        }
    }

    void download_file_tail(const std::string& root, const std::string& path, size_t max_bytes,
                            StringCallback on_success, ErrorCallback on_error) override {
        auto it = files_.find(path);
        if (it == files_.end()) {
            MoonrakerFileTransferAPIMock::download_file_tail(
                root, path, max_bytes, std::move(on_success), std::move(on_error));
            return;
        }
        ++tails;
        const std::string& f = it->second;
        on_success(f.substr(f.size() - std::min(max_bytes, f.size())));
    }

    void download_file_to_path(const std::string& root, const std::string& path,
                               const std::string& dest_path, StringCallback on_success,
                               ErrorCallback on_error, ProgressCallback on_progress) override {
        auto it = files_.find(path);
        if (it == files_.end()) {
            MoonrakerFileTransferAPIMock::download_file_to_path(
                root, path, dest_path, std::move(on_success), std::move(on_error),
                std::move(on_progress));
            return;
        }
        ++whole_downloads;
        downloaded_to.push_back(dest_path);
        std::ofstream out(dest_path, std::ios::binary);
        out << it->second;
        out.close();
        on_success(dest_path);
    }

    bool ranged = true; ///< What supports_range_reads() answers
    int ranges = 0;
    int tails = 0;
    size_t range_bytes = 0;
    int whole_downloads = 0;
    bool fail_ranges = false;
    int fail_from = 0; ///< Ranged reads from this one on (1-based) fail; 0 = none
    int hold_from = 0; ///< Ranged reads from this one on wait for release_held(); 0 = none
    std::vector<std::function<void(bool)>> held;
    CancelFlag last_cancel; ///< The cancel flag the last ranged read was given
    std::vector<std::pair<uint64_t, size_t>> range_requests; ///< offset, length of each
    std::vector<std::string> downloaded_to;

  private:
    std::map<std::string, std::string> files_;
};

/// MoonrakerAPIMock whose transfers() serves an ArchiveTransfers.
class ArchiveMoonrakerAPIMock : public MoonrakerAPIMock {
  public:
    ArchiveMoonrakerAPIMock(helix::MoonrakerClient& client, helix::PrinterState& state,
                            ArchiveTransfers& transfers)
        : MoonrakerAPIMock(client, state), transfers_(transfers) {}

    MoonrakerFileTransferAPI& transfers() override {
        return transfers_;
    }

  private:
    ArchiveTransfers& transfers_;
};
