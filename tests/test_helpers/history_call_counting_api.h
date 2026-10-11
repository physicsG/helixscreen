// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "moonraker_api.h"
#include "moonraker_error.h"
#include "moonraker_file_api.h"
#include "moonraker_history_api.h"

#include <atomic>
#include <functional>
#include <map>
#include <utility>
#include <vector>

namespace helix {

/// Records the history requests that actually reach the client.
///
/// Lets a test assert on requests issued rather than on their downstream
/// effects, which is the only way to tell a load apart from a no-op on a mock
/// whose history is short enough that every scope returns the same jobs.
class HistoryCallCountingAPI : public MoonrakerHistoryAPI {
  public:
    explicit HistoryCallCountingAPI(helix::IMoonrakerClient& client)
        : MoonrakerHistoryAPI(client) {}

    void get_history_list(int limit, int start, double since, double before,
                          HistoryListCallback on_success, ErrorCallback on_error) override {
        ++calls;
        last_limit.store(limit);
        MoonrakerHistoryAPI::get_history_list(limit, start, since, before, std::move(on_success),
                                              std::move(on_error));
    }

    std::atomic<int> calls{0};
    std::atomic<int> last_limit{0};
};

/// MoonrakerAPI that installs the counting history API in place of the real one.
/// Answers file metadata from a table on the calling thread: a filename it
/// holds answers with that metadata, anything else as missing.
class MetadataTableFileAPI : public MoonrakerFileAPI {
  public:
    explicit MetadataTableFileAPI(helix::IMoonrakerClient& client) : MoonrakerFileAPI(client) {}

    void get_file_metadata(const std::string& filename, FileMetadataCallback on_success,
                           ErrorCallback on_error, bool /*silent*/ = false) override {
        ++calls;
        // The answer is decided when the request is made, as a server would.
        std::function<void()> answer;
        auto it = table.find(filename);
        if (it != table.end()) {
            answer = [on_success, meta = it->second]() { on_success(meta); };
        } else {
            answer = [on_error]() {
                if (on_error) {
                    on_error(
                        MoonrakerError::json_rpc_error("server.files.metadata", "File not found"));
                }
            };
        }
        if (hold) {
            held.push_back(std::move(answer));
        } else {
            answer();
        }
    }

    /// Deliver the held answer to the @p index-th request, in any order.
    void release(size_t index) {
        held.at(index)();
    }

    std::map<std::string, FileMetadata> table;
    int calls = 0;
    bool hold = false;
    std::vector<std::function<void()>> held;
};

class HistoryCallCountingMoonrakerAPI : public MoonrakerAPI {
  public:
    HistoryCallCountingMoonrakerAPI(helix::IMoonrakerClient& client, helix::PrinterState& state)
        : MoonrakerAPI(client, state) {
        // history_api_ is protected; swap in the counting implementation.
        history_api_ = std::make_unique<HistoryCallCountingAPI>(client);
        file_api_ = std::make_unique<MetadataTableFileAPI>(client);
    }

    [[nodiscard]] MetadataTableFileAPI& metadata_table() {
        return static_cast<MetadataTableFileAPI&>(*file_api_);
    }

    [[nodiscard]] int history_list_calls() const {
        return counting()->calls.load();
    }

    /// Job limit the most recent request carried.
    [[nodiscard]] int history_last_limit() const {
        return counting()->last_limit.load();
    }

  private:
    [[nodiscard]] const HistoryCallCountingAPI* counting() const {
        return static_cast<const HistoryCallCountingAPI*>(history_api_.get());
    }
};

} // namespace helix

namespace helix {

/// Holds every history list request until the test answers it, so a case can
/// read what each request asked for (limit, `before`) and feed the response.
class ScriptedHistoryAPI : public MoonrakerHistoryAPI {
  public:
    struct Request {
        int limit;
        int start;
        double since;
        double before;
        HistoryListCallback on_success;
        ErrorCallback on_error;
    };

    explicit ScriptedHistoryAPI(helix::IMoonrakerClient& client) : MoonrakerHistoryAPI(client) {}

    void get_history_list(int limit, int start, double since, double before,
                          HistoryListCallback on_success, ErrorCallback on_error) override {
        requests.push_back(
            {limit, start, since, before, std::move(on_success), std::move(on_error)});
    }

    /// Answer the oldest outstanding request with `jobs`.
    void answer(const std::vector<PrintHistoryJob>& jobs) {
        Request r = std::move(requests.front());
        requests.erase(requests.begin());
        r.on_success(jobs, jobs.size());
    }

    std::vector<Request> requests;
};

/// MoonrakerAPI that installs the scripted history API in place of the real one.
class ScriptedHistoryMoonrakerAPI : public MoonrakerAPI {
  public:
    ScriptedHistoryMoonrakerAPI(helix::IMoonrakerClient& client, helix::PrinterState& state)
        : MoonrakerAPI(client, state) {
        history_api_ = std::make_unique<ScriptedHistoryAPI>(client);
        file_api_ = std::make_unique<MetadataTableFileAPI>(client);
    }

    ScriptedHistoryAPI& scripted() {
        return *static_cast<ScriptedHistoryAPI*>(history_api_.get());
    }

    [[nodiscard]] MetadataTableFileAPI& metadata_table() {
        return static_cast<MetadataTableFileAPI&>(*file_api_);
    }
};

} // namespace helix
