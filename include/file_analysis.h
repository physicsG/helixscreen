// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The HelixPrint plugin's file analysis (feature `file_analysis`): one record
// per G-code file, written on the printer after upload. The record's shape is
// documented in moonraker-plugin/helix_analysis.py and the plugin README; this
// reads the parts the screen uses, and decides whether a caller asks for it.

#include "fit_check.h"
#include "helix_plugin_features.h"
#include "json_fwd.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace helix {

class IMoonrakerAPI;
class LifetimeToken;
struct FileStartFacts;

inline constexpr const char* kFileAnalysisFeature = "file_analysis";
inline constexpr int kFileAnalysisMinVersion = 1;
inline constexpr const char* kFileAnalysisRpc = "server.helix.analysis";
inline constexpr int kFileAnalysisSchema = 1;

/// Where a caller takes a file's analysis from.
enum class FileAnalysisSource {
    Plugin,   ///< ask server.helix.analysis first
    Fallback, ///< read the file the way the screen does without the plugin
};

/// The one plugin-or-fallback decision: the plugin answers for a G-code file
/// while its `file_analysis` feature is Ready. A `.3mf` has no record.
[[nodiscard]] FileAnalysisSource file_analysis_source(PluginFeatureState feature,
                                                      const std::string& filename);

/// One tool's line of the record. A value the file does not answer is unset.
struct FileAnalysisTool {
    int tool = 0;
    std::optional<double> mm;
    std::optional<double> grams;
    std::string color; ///< `#RRGGBB[AA]`, or empty
    std::string material;
};

struct FileAnalysisObject {
    std::string name;
    std::optional<std::pair<double, double>> center;
    std::vector<std::pair<double, double>> polygon;
};

/// The parts of a record the screen reads.
struct FileAnalysisRecord {
    std::string filename;
    uint64_t file_size = 0;
    double file_mtime = 0.0;
    std::vector<FileAnalysisTool> tools; ///< one per tool the file prints with, ascending
    std::string objects_source;          ///< "define", "label" or "none"
    std::vector<FileAnalysisObject> objects;
    int layer_count = 0;
    std::string layer_index; ///< gcodes-relative sidecar path, or empty
    int pauses = 0;
    /// The record's `bounds`, `header` and tool numbers, as the fit check reads them.
    FitFile fit;
};

/// What server.helix.analysis answered.
struct FileAnalysisReply {
    enum class Status {
        Ready,    ///< `record` holds the file's current analysis
        Pending,  ///< queued on the printer; helix:analysis_ready follows
        Disabled, ///< the user switched the feature off
        Invalid,  ///< no status this screen understands, or a record of another schema
    };
    Status status = Status::Invalid;
    std::optional<FileAnalysisRecord> record;
};

/// Read a server.helix.analysis `result`. Never throws.
[[nodiscard]] FileAnalysisReply parse_file_analysis_reply(const nlohmann::json& result);

/// Take what @p record answers into @p facts, field by field: the tools it
/// prints with, each tool's color and material (a slot the record leaves empty
/// keeps the metadata's), per-tool grams when the record has grams for
/// every tool it prints with, and the fit check's file facts. A field the record does not answer is
/// left as it was, for the screen's own reads to fill.
void apply_file_analysis(FileStartFacts& facts, const FileAnalysisRecord& record);

/// Ask the plugin for @p filename's analysis. @p on_reply runs on a
/// background thread with what it answered (an Invalid reply when the call
/// failed); the caller moves it to the main thread.
void request_file_analysis(IMoonrakerAPI* api, const std::string& filename,
                           std::function<void(FileAnalysisReply)> on_reply);

/// Hand @p load the text of @p filename's layer index sidecar
/// (GCodeLayerIndex::build_from_sidecar) when the plugin's current record
/// names one, else an empty string. Call it on the main thread. When the
/// load will not use a sidecar (@p wanted false: only a streaming load does)
/// or file_analysis_source() says not to ask the plugin, @p load runs at once,
/// so the preview never waits on the plugin for nothing; otherwise it runs on
/// the main thread once the plugin answered, unless @p tok has expired by then.
void with_layer_index_sidecar(IMoonrakerAPI* api, PluginFeatureState feature,
                              const std::string& filename, bool wanted, const LifetimeToken& tok,
                              std::function<void(std::string sidecar)> load);

} // namespace helix
