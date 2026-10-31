// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "helix_shared_callback.h"

/**
 * @file delayed_file_transfers.h
 * @brief A mock transfer API whose downloads, preamble and footer reads a
 *        test can hold and release, and the MoonrakerAPIMock that serves it.
 */

#include "moonraker_api_mock.h"
#include "moonraker_client_mock.h"
#include "printer_state.h"

#include <functional>
#include <string>
#include <utility>
#include <vector>

/// MoonrakerFileTransferAPIMock with an opt-in transfer HOLD. While
/// hold_transfers is set, download_file_to_path captures the request instead
/// of resolving it; the test later releases the queue to simulate a slow
/// network finishing. Lets the preflight timeout fire genuinely mid-download
/// — the exact compound race the settlement gate guards against.
class DelayedFileTransfers : public MoonrakerFileTransferAPIMock {
  public:
    using MoonrakerFileTransferAPIMock::MoonrakerFileTransferAPIMock;

    void download_file_to_path(const std::string& root, const std::string& path,
                               const std::string& dest_path, StringCallback on_success,
                               ErrorCallback on_error, ProgressCallback on_progress) override {
        (void)on_progress;
        ++download_count;
        if (!hold_transfers) {
            MoonrakerFileTransferAPIMock::download_file_to_path(
                root, path, dest_path, std::move(on_success), std::move(on_error));
            return;
        }
        held_.push_back({path, [this, root, path, dest_path, on_success = std::move(on_success),
                                on_error = std::move(on_error)]() {
                             MoonrakerFileTransferAPIMock::download_file_to_path(
                                 root, path, dest_path, on_success, on_error);
                         }});
    }

    /// When set, every footer read reports failure. The detail view then
    /// falls back to the whole-file download + Tn scan — the path these
    /// cache-poisoning tests were written against, and the one a slicer that
    /// writes no per-tool usage line still takes in production.
    void download_file_tail(const std::string& root, const std::string& path, size_t max_bytes,
                            StringCallback on_success, ErrorCallback on_error) override {
        ++tail_read_count;
        if (fail_tail_reads) {
            if (on_error) {
                on_error(MoonrakerError::file_not_found("download_file_tail",
                                                        "footer read disabled by test"));
            }
            return;
        }
        MoonrakerFileTransferAPIMock::download_file_tail(
            root, path, max_bytes, std::move(on_success), std::move(on_error));
    }

    /// Holds the preamble read the operations scan makes, the way hold_transfers
    /// holds the whole-file download.
    void download_file_partial(const std::string& root, const std::string& path, size_t max_bytes,
                               StringCallback on_success, ErrorCallback on_error,
                               CancelFlag = nullptr) override {
        ++partial_read_count;
        if (!hold_partials) {
            MoonrakerFileTransferAPIMock::download_file_partial(
                root, path, max_bytes, std::move(on_success), std::move(on_error));
            return;
        }
        held_partials_.push_back([this, root, path, max_bytes, on_success = std::move(on_success),
                                  on_error = std::move(on_error)]() {
            MoonrakerFileTransferAPIMock::download_file_partial(root, path, max_bytes, on_success,
                                                                on_error);
        });
    }

    void release_held_partials() {
        auto held = std::move(held_partials_);
        held_partials_.clear();
        for (auto& h : held) {
            h();
        }
    }

    bool hold_transfers = false;
    bool hold_partials = false;
    bool fail_tail_reads = false;

    /// A transport that writes no local copies and reads no tails, as on the ESP32.
    bool local_copies = true;
    bool tail_reads = true;
    bool supports_local_copies() const override {
        return local_copies;
    }
    bool supports_tail_reads() const override {
        return tail_reads;
    }

    /// How many footer reads the view has asked for. The tools-used cache
    /// persists only the used-tool set, so a cache hit must NOT suppress the
    /// read that also answers the palette and the per-tool grams.
    int tail_read_count = 0;
    /// Whole-file downloads and preamble reads, counted the same way.
    int download_count = 0;
    int partial_read_count = 0;

    /// Resolve every held transfer (copies the real asset, fires callbacks
    /// synchronously — same as an unheld call).
    void release_held() {
        auto held = std::move(held_);
        held_.clear();
        for (auto& h : held) {
            h.finish();
        }
    }

    /// Complete the held download of @p path alone, so two downloads can land
    /// in either order. False when none is held.
    bool release(const std::string& path) {
        for (auto it = held_.begin(); it != held_.end(); ++it) {
            if (it->path == path) {
                auto finish = std::move(it->finish);
                held_.erase(it);
                finish();
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] size_t held_count() const {
        return held_.size();
    }

  private:
    struct Held {
        std::string path;
        helix::SharedCallback<void()> finish;
    };
    std::vector<Held> held_;
    std::vector<helix::SharedCallback<void()>> held_partials_;
};

/// MoonrakerAPIMock whose transfers() serves the holdable DelayedFileTransfers
/// instance (everything else behaves identically to the plain mock).
class DelayedMoonrakerAPIMock : public MoonrakerAPIMock {
  public:
    DelayedMoonrakerAPIMock(helix::MoonrakerClient& client, helix::PrinterState& state,
                            DelayedFileTransfers& transfers)
        : MoonrakerAPIMock(client, state), transfers_(transfers) {}

    MoonrakerFileTransferAPI& transfers() override {
        return transfers_;
    }

  private:
    DelayedFileTransfers& transfers_;
};
