// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "moonraker_api_mock.h"
#include "moonraker_client_mock.h"
#include "moonraker_file_api.h"
#include "print_completion.h"
#include "printer_state.h"

#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "../catch_amalgamated.hpp"

using namespace helix;

namespace {

/// Answers list_files the way Moonraker does (paths relative to the root, the
/// whole tree whatever directory was asked for) and records deletes.
class StagingFileAPI : public MoonrakerFileAPI {
  public:
    explicit StagingFileAPI(MoonrakerClient& client) : MoonrakerFileAPI(client) {}

    void list_files(const std::string& root, const std::string& path, bool /*recursive*/,
                    FileListCallback on_success, ErrorCallback /*on_error*/) override {
        listed.push_back(root + ":" + path);
        std::vector<FileInfo> files;
        for (const char* p :
             {".helix_temp/modified_1p_a.gcode", "helix_staging/modified_2p_b.gcode",
              "helix_print/full/c.gcode", "parts/d.gcode", "e.gcode"}) {
            FileInfo f;
            f.path = p;
            f.filename = p;
            files.push_back(f);
        }
        on_success(files);
    }

    void delete_file(const std::string& filename, SuccessCallback on_success,
                     ErrorCallback /*on_error*/) override {
        deleted.push_back(filename);
        if (on_success) {
            on_success();
        }
    }

    std::vector<std::string> listed;
    std::vector<std::string> deleted;
};

class StagingAPI : public MoonrakerAPIMock {
  public:
    StagingAPI(MoonrakerClient& client, PrinterState& state) : MoonrakerAPIMock(client, state) {
        auto f = std::make_unique<StagingFileAPI>(client);
        staging = f.get();
        file_api_ = std::move(f);
    }
    StagingFileAPI* staging = nullptr;
};

} // namespace

TEST_CASE("the startup sweep empties every staging directory and nothing else",
          "[print_completion][staging]") {
    MoonrakerClientMock client(MoonrakerClientMock::PrinterType::VORON_24);
    PrinterState state;
    state.init_subjects(false);
    StagingAPI api(client, state);

    cleanup_stale_helix_temp_files(&api);

    CHECK(api.staging->listed ==
          std::vector<std::string>{"gcodes:.helix_temp", "gcodes:helix_staging"});
    auto deleted = api.staging->deleted;
    std::sort(deleted.begin(), deleted.end());
    CHECK(deleted == std::vector<std::string>{"gcodes/.helix_temp/modified_1p_a.gcode",
                                              "gcodes/helix_staging/modified_2p_b.gcode"});
}
