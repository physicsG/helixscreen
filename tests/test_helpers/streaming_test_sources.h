// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "gcode_data_source.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <unistd.h>
#include <vector>

namespace helix::gcode::test {

/// Writes `content` to a temp file for the streaming controller to index.
class TempGCode {
  public:
    explicit TempGCode(const std::string& content)
        : path_("/tmp/test_stream_src_" + std::to_string(::getpid()) + "_" +
                std::to_string(counter_++) + ".gcode") {
        std::ofstream(path_) << content;
    }
    ~TempGCode() {
        std::remove(path_.c_str());
    }
    const std::string& path() const {
        return path_;
    }

  private:
    static inline std::atomic<int> counter_{0};
    std::string path_;
};

/// Reads at or past `fail_from` come back empty, as a read error does: the
/// first `failures` of them, or every one when `failures` is negative.
class FailingSource : public FileDataSource {
  public:
    FailingSource(const std::string& path, uint64_t fail_from, int failures)
        : FileDataSource(path), fail_from_(fail_from), failures_left_(failures) {}

    std::vector<char> read_range(uint64_t offset, uint32_t length) override {
        if (offset >= fail_from_ && failures_left_ != 0) {
            if (failures_left_ > 0) {
                --failures_left_;
            }
            return {};
        }
        return FileDataSource::read_range(offset, length);
    }

  private:
    uint64_t fail_from_;
    std::atomic<int> failures_left_;
};

/// A straight 20mm outer-wall tower with 1mm layers; `layer_start[i]` is the
/// byte offset where layer i begins.
inline std::string make_tower_text(int layers, std::vector<size_t>& layer_start) {
    std::string out = "; streaming tower fixture\nM82\nG28\n";
    float e = 0.0f;
    for (int i = 0; i < layers; ++i) {
        layer_start.push_back(out.size());
        out += "G1 Z" + std::to_string(1.0f + static_cast<float>(i)) + " F1000\n";
        out += ";TYPE:Outer wall\nG1 X20 Y20 F3000\n";
        const char* corners[] = {"X40 Y20", "X40 Y40", "X20 Y40", "X20 Y20"};
        for (const char* c : corners) {
            e += 1.0f;
            out += std::string("G1 ") + c + " E" + std::to_string(e) + "\n";
        }
    }
    return out;
}

} // namespace helix::gcode::test
