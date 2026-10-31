// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "async_lifetime_guard.h"
#include "storage_report.h"

#include <array>
#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace helix {
class IMoonrakerClient;
}

namespace helix::storage {

/// Fetches the storage report from the plugin or, without it, lists the
/// known directories over the file API and sums them; runs a consumer's
/// cleanup either way. Callbacks run on the main thread, and never after the
/// service is destroyed. A new fetch abandons the fetch in flight and a new
/// cleanup the cleanup in flight; a fetch never cancels a cleanup.
class StorageReportService {
  public:
    using ReportCallback =
        std::function<void(ReportSource source, std::optional<StorageReport> report)>;
    using CleanupCallback = std::function<void(const CleanupOutcome& outcome)>;

    explicit StorageReportService(IMoonrakerClient* client) : client_(client) {}

    /// @p may_clean is fallback_may_clean() for this printer. The FileApi
    /// fetch also reads server.files.roots, and offers the timelapse cleanup
    /// only when @p may_clean and timelapse_root_safe() both hold.
    void fetch(ReportSource source, bool may_clean, ReportCallback done);

    /// Remove consumer @p index's files. The fallback removes only what
    /// fallback_removes() allows of the files the last FileApi fetch listed.
    void cleanup(ReportSource source, std::size_t index, CleanupCallback done);

  private:
    struct Pending {
        Root root;
        std::string rel; ///< relative to the root, empty for the root itself
    };
    struct Known {
        std::string path; ///< root-prefixed, as server.files.delete_file takes it
        ListedFile file;
    };

    void start_listing();
    void list_next();
    void on_listing(const Pending& dir, const std::optional<DirectoryListing>& listing);
    void finish_scan();
    void delete_next();

    IMoonrakerClient* client_;
    AsyncLifetimeGuard fetch_lifetime_;
    AsyncLifetimeGuard cleanup_lifetime_;

    // Fallback scan in flight
    ReportCallback report_done_;
    std::deque<Pending> pending_;
    std::size_t listed_dirs_ = 0;
    StorageReport scan_;
    /// The last FileApi fetch found the timelapse root safe to clean
    bool timelapse_cleanable_ = false;

    /// Files the last fallback scan found, per fallback-cleanable consumer
    std::array<std::vector<Known>, kConsumerCount> known_{};

    // Fallback cleanup in flight
    std::vector<Known> to_delete_;
    CleanupOutcome outcome_;
    CleanupCallback cleanup_done_;
};

/// Read the free space of the volume holding gcodes directory @p dir (the
/// directory the upload goes to; the gcodes root when it does not exist yet)
/// and answer whether an upload of @p size bytes may go ahead (upload_space).
/// Listing @p dir rather than the root keeps the request small. @p done runs
/// on the main thread while @p token is live. Unknown free space answers
/// Unknown, which callers treat as Ok.
void check_upload_space(IMoonrakerClient* client, const std::string& dir, uint64_t size,
                        LifetimeToken token, std::function<void(UploadSpace)> done);

} // namespace helix::storage
