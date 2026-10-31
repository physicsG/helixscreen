// SPDX-License-Identifier: GPL-3.0-or-later

#include "storage_report_service.h"

#include "i_moonraker_client.h"
#include "json_utils.h"
#include "moonraker_error.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <ctime>

namespace helix::storage {

namespace {

const char* root_name(Root root) {
    switch (root) {
    case Root::Gcodes:
        return "gcodes";
    case Root::Timelapse:
        return "timelapse";
    case Root::Logs:
        return "logs";
    case Root::Data:
        break;
    }
    return "";
}

std::string join_path(const std::string& a, const std::string& b) {
    if (a.empty() || b.empty()) {
        return a.empty() ? b : a;
    }
    return a + "/" + b;
}

const nlohmann::json& result_of(const nlohmann::json& response) {
    const nlohmann::json* result = json_util::find_member(response, "result");
    return result != nullptr ? *result : response;
}

} // namespace

void StorageReportService::fetch(ReportSource source, bool may_clean, ReportCallback done) {
    fetch_lifetime_.invalidate();
    if (client_ == nullptr) {
        done(source, std::nullopt);
        return;
    }
    if (source == ReportSource::Plugin) {
        client_->send_jsonrpc(
            "server.helix.storage", nlohmann::json::object(),
            fetch_lifetime_.bg_cb("StorageReportService::report",
                                  [done](const nlohmann::json& response) {
                                      done(ReportSource::Plugin,
                                           parse_storage_result(result_of(response)));
                                  }),
            fetch_lifetime_.bg_cb("StorageReportService::report_error",
                                  [done](const MoonrakerError& err) {
                                      spdlog::warn("[Storage] Plugin report failed: {}",
                                                   err.message);
                                      done(ReportSource::Plugin, std::nullopt);
                                  }),
            0, /*silent=*/true);
        return;
    }

    report_done_ = std::move(done);
    scan_ = StorageReport{};
    listed_dirs_ = 0;
    timelapse_cleanable_ = false;
    for (auto& known : known_) {
        known.clear();
    }
    if (!may_clean) {
        start_listing();
        return;
    }
    client_->send_jsonrpc(
        "server.files.roots", nlohmann::json::object(),
        fetch_lifetime_.bg_cb("StorageReportService::roots",
                              [this](const nlohmann::json& response) {
                                  timelapse_cleanable_ = timelapse_root_safe(result_of(response));
                                  start_listing();
                              }),
        fetch_lifetime_.bg_cb("StorageReportService::roots_error",
                              [this](const MoonrakerError& err) {
                                  spdlog::debug("[Storage] Roots unreadable ({}); timelapse "
                                                "stays report-only",
                                                err.message);
                                  start_listing();
                              }),
        0, /*silent=*/true);
}

void StorageReportService::start_listing() {
    pending_ = {{Root::Gcodes, ""}, {Root::Timelapse, ""}, {Root::Logs, ""}};
    list_next();
}

void StorageReportService::list_next() {
    if (pending_.empty()) {
        finish_scan();
        return;
    }
    if (listed_dirs_ >= kMaxFallbackDirectories) {
        scan_.partial = true;
        pending_.clear();
        finish_scan();
        return;
    }
    const Pending dir = pending_.front();
    pending_.pop_front();
    ++listed_dirs_;
    const std::string path = join_path(root_name(dir.root), dir.rel);
    client_->send_jsonrpc(
        "server.files.get_directory", nlohmann::json{{"path", path}},
        fetch_lifetime_.bg_cb("StorageReportService::listing",
                              [this, dir](const nlohmann::json& response) {
                                  on_listing(dir, parse_directory_result(result_of(response)));
                              }),
        fetch_lifetime_.bg_cb("StorageReportService::listing_error",
                              [this, dir, path](const MoonrakerError& err) {
                                  spdlog::debug("[Storage] Listing {} failed: {}", path,
                                                err.message);
                                  on_listing(dir, std::nullopt);
                              }),
        0, /*silent=*/true);
}

void StorageReportService::on_listing(const Pending& dir,
                                      const std::optional<DirectoryListing>& listing) {
    if (listing) {
        const bool at_root = dir.rel.empty();
        if (at_root && dir.root == Root::Timelapse && !timelapse_listing_is_own(*listing)) {
            // Another directory reached under another spelling: report only.
            timelapse_cleanable_ = false;
        }
        if (at_root) {
            for (std::size_t i = 0; i < kConsumerCount; ++i) {
                const ConsumerInfo& info = kConsumers[i];
                // Without the plugin no file is a .3mf output, and no file root
                // reaches power-loss recovery's directory.
                const bool measured =
                    info.root == dir.root && std::string_view(info.key) != "3mf_outputs";
                if (measured) {
                    scan_.consumers[i].available = true;
                    scan_.consumers[i].cleanable = fallback_cleanable(i) && timelapse_cleanable_;
                }
            }
            if (dir.root == Root::Gcodes) {
                scan_.total = listing->total;
                scan_.free = listing->free;
            }
        }
        for (const ListedFile& f : listing->files) {
            const std::string rel = join_path(dir.rel, f.name);
            std::size_t index = 0;
            if (dir.root == Root::Gcodes) {
                index = classify_gcodes_path(rel);
            } else {
                index =
                    consumer_index(dir.root == Root::Timelapse ? "timelapse" : "logs").value_or(0);
            }
            ConsumerUsage& usage = scan_.consumers[index];
            usage.bytes += f.size;
            usage.files += 1;
            if (fallback_cleanable(index) && timelapse_cleanable_) {
                ListedFile file = f;
                file.name = rel;
                known_[index].push_back({join_path(root_name(dir.root), rel), std::move(file)});
            }
        }
        for (const std::string& sub : listing->dirs) {
            pending_.push_back({dir.root, join_path(dir.rel, sub)});
        }
    }
    list_next();
}

void StorageReportService::finish_scan() {
    ReportCallback done = std::move(report_done_);
    report_done_ = nullptr;
    const bool any = std::any_of(scan_.consumers.begin(), scan_.consumers.end(),
                                 [](const ConsumerUsage& u) { return u.available; });
    if (done) {
        done(ReportSource::FileApi, any ? std::optional<StorageReport>(scan_) : std::nullopt);
    }
}

void StorageReportService::cleanup(ReportSource source, std::size_t index, CleanupCallback done) {
    cleanup_lifetime_.invalidate();
    CleanupOutcome refused;
    refused.refused = true;
    if (client_ == nullptr || index >= kConsumerCount || !kConsumers[index].cleanable) {
        done(refused);
        return;
    }
    if (source == ReportSource::Plugin) {
        client_->send_jsonrpc(
            "server.helix.storage_cleanup",
            nlohmann::json{{"consumer", kConsumers[index].key}, {"older_than", 0}},
            cleanup_lifetime_.bg_cb("StorageReportService::cleanup",
                                    [done](const nlohmann::json& response) {
                                        const nlohmann::json& result = result_of(response);
                                        CleanupOutcome outcome;
                                        outcome.freed = json_util::safe_uint64(result, "freed");
                                        outcome.errors = json_util::safe_uint64(result, "errors");
                                        done(outcome);
                                    }),
            cleanup_lifetime_.bg_cb("StorageReportService::cleanup_error",
                                    [done, refused](const MoonrakerError& err) {
                                        spdlog::warn("[Storage] Plugin cleanup failed: {}",
                                                     err.message);
                                        done(refused);
                                    }),
            kCleanupRpcTimeoutMs, /*silent=*/true);
        return;
    }
    if (!fallback_cleanable(index) || !timelapse_cleanable_) {
        done(refused);
        return;
    }

    const double now = static_cast<double>(std::time(nullptr));
    to_delete_.clear();
    for (const Known& k : known_[index]) {
        if (fallback_removes(index, k.file, now, 0.0)) {
            to_delete_.push_back(k);
        }
    }
    known_[index].clear();
    outcome_ = CleanupOutcome{};
    cleanup_done_ = std::move(done);
    delete_next();
}

void StorageReportService::delete_next() {
    if (to_delete_.empty()) {
        CleanupCallback done = std::move(cleanup_done_);
        cleanup_done_ = nullptr;
        if (done) {
            done(outcome_);
        }
        return;
    }
    const Known k = to_delete_.back();
    to_delete_.pop_back();
    client_->send_jsonrpc(
        "server.files.delete_file", nlohmann::json{{"path", k.path}},
        cleanup_lifetime_.bg_cb("StorageReportService::deleted",
                                [this, size = k.file.size](const nlohmann::json&) {
                                    outcome_.freed += size;
                                    delete_next();
                                }),
        cleanup_lifetime_.bg_cb("StorageReportService::delete_error",
                                [this, path = k.path](const MoonrakerError& err) {
                                    spdlog::warn("[Storage] Could not delete {}: {}", path,
                                                 err.message);
                                    outcome_.errors += 1;
                                    delete_next();
                                }),
        kCleanupRpcTimeoutMs, /*silent=*/true);
}

namespace {

void read_free_space(IMoonrakerClient* client, const std::string& path, bool fall_back_to_root,
                     uint64_t size, LifetimeToken token, std::function<void(UploadSpace)> done) {
    client->send_jsonrpc(
        "server.files.get_directory", nlohmann::json{{"path", path}},
        [token, size, done](const nlohmann::json& response) {
            const auto listing = parse_directory_result(result_of(response));
            const UploadSpace verdict = upload_space(listing ? listing->free : std::nullopt, size);
            token.defer("storage::check_upload_space", [done, verdict]() { done(verdict); });
        },
        [client, fall_back_to_root, size, token, done](const MoonrakerError& err) {
            if (fall_back_to_root) {
                // The directory is not there yet: the gcodes root is on the same volume.
                token.defer("storage::check_upload_space_root", [client, size, token, done]() {
                    read_free_space(client, "gcodes", false, size, token, done);
                });
                return;
            }
            spdlog::debug("[Storage] Free space unreadable before upload: {}", err.message);
            token.defer("storage::check_upload_space_error",
                        [done]() { done(UploadSpace::Unknown); });
        },
        0, /*silent=*/true);
}

} // namespace

void check_upload_space(IMoonrakerClient* client, const std::string& dir, uint64_t size,
                        LifetimeToken token, std::function<void(UploadSpace)> done) {
    if (client == nullptr) {
        done(UploadSpace::Unknown);
        return;
    }
    const bool in_root = dir.empty();
    read_free_space(client, in_root ? "gcodes" : join_path("gcodes", dir), !in_root, size,
                    std::move(token), std::move(done));
}

} // namespace helix::storage
