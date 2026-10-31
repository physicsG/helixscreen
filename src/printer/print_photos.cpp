// SPDX-License-Identifier: GPL-3.0-or-later

#include "print_photos.h"

#include "json_utils.h"

#include <spdlog/fmt/fmt.h>

#include <algorithm>
#include <cmath>
#include <utility>

namespace helix {

namespace {

bool known_kind(const std::string& kind) {
    return kind == kPhotoKindLayer || kind == kPhotoKindEnd;
}

// Layer photo first, as the plugin orders them.
void sort_photos(std::vector<PrintPhoto>& photos) {
    std::stable_sort(photos.begin(), photos.end(), [](const PrintPhoto& a, const PrintPhoto& b) {
        return a.kind == kPhotoKindLayer && b.kind != kPhotoKindLayer;
    });
}

} // namespace

std::string photo_kind_from_path(const std::string& path) {
    for (const char* kind : {kPhotoKindLayer, kPhotoKindEnd}) {
        const std::string suffix = std::string("_") + kind + ".jpg";
        if (path.size() > suffix.size() &&
            path.compare(path.size() - suffix.size(), suffix.size(), suffix) == 0) {
            return kind;
        }
    }
    return "";
}

std::string photo_file_name(const std::string& job_id, double start_time, const std::string& kind) {
    const long long start = std::isfinite(start_time) ? static_cast<long long>(start_time) : 0;
    return fmt::format("{}_{}_{}.jpg", job_id, start, kind);
}

std::string photo_path(const std::string& job_id, double start_time, const std::string& kind) {
    return std::string(kPhotosDir) + "/" + photo_file_name(job_id, start_time, kind);
}

bool is_photo_path(const std::string& path) {
    const std::string prefix = std::string(kPhotosDir) + "/";
    if (path.compare(0, prefix.size(), prefix) != 0) {
        return false;
    }
    const std::string name = path.substr(prefix.size());
    return !name.empty() && name.find('/') == std::string::npos && name != "." && name != "..";
}

bool layer_two_done(std::optional<int> previous, int current) {
    return previous && *previous >= 1 && *previous <= 2 && current > 2;
}

bool ends_with_photo(const std::string& history_status) {
    return history_status == "completed" || history_status == "cancelled" ||
           history_status == "error";
}

PhotoTaker photo_taker(PluginFeatureState photos, bool opted_in, bool can_capture,
                       FallbackSenderRole role) {
    switch (photos) {
    case PluginFeatureState::Ready:
        return PhotoTaker::Plugin;
    case PluginFeatureState::Disabled:
        // The plugin can take them and the user switched them off.
        return PhotoTaker::None;
    case PluginFeatureState::Absent:
        return opted_in && can_capture && role == FallbackSenderRole::Sender ? PhotoTaker::App
                                                                             : PhotoTaker::None;
    }
    return PhotoTaker::None;
}

std::vector<PrintPhoto> parse_photo_record(const nlohmann::json& record) {
    std::vector<PrintPhoto> photos;
    if (!record.is_object() || json_util::safe_int(record, "version", 0) != kPhotoRecordVersion) {
        return photos;
    }
    const nlohmann::json* list = json_util::find_member(record, "photos");
    if (list == nullptr || !list->is_array()) {
        return photos;
    }
    for (const auto& entry : *list) {
        PrintPhoto photo;
        photo.kind = json_util::safe_string(entry, "kind", "");
        photo.path = json_util::safe_string(entry, "path", "");
        if (known_kind(photo.kind) && is_photo_path(photo.path)) {
            photos.push_back(std::move(photo));
        }
    }
    sort_photos(photos);
    return photos;
}

std::vector<PrintPhoto> parse_photo_aux(const nlohmann::json& auxiliary_data) {
    std::vector<PrintPhoto> photos;
    if (!auxiliary_data.is_array()) {
        return photos;
    }
    for (const auto& entry : auxiliary_data) {
        if (json_util::safe_string(entry, "provider", "") != kPhotosAuxProvider ||
            json_util::safe_string(entry, "name", "") != kPhotosAuxName) {
            continue;
        }
        const nlohmann::json* value = json_util::find_member(entry, "value");
        if (value == nullptr || !value->is_array()) {
            continue;
        }
        for (const auto& item : *value) {
            PrintPhoto photo;
            photo.path = json_util::as_string(item, "");
            photo.kind = photo_kind_from_path(photo.path);
            if (known_kind(photo.kind) && is_photo_path(photo.path)) {
                photos.push_back(std::move(photo));
            }
        }
    }
    sort_photos(photos);
    return photos;
}

std::vector<PrintPhoto> merge_photo_lists(const std::vector<PrintPhoto>& linked,
                                          const std::vector<PrintPhoto>& recorded) {
    std::vector<PrintPhoto> out = recorded;
    for (const auto& photo : linked) {
        const bool known = std::any_of(
            out.begin(), out.end(), [&photo](const PrintPhoto& p) { return p.kind == photo.kind; });
        if (!known) {
            out.push_back(photo);
        }
    }
    sort_photos(out);
    return out;
}

bool same_photos(const std::vector<PrintPhoto>& a, const std::vector<PrintPhoto>& b) {
    return a.size() == b.size() &&
           std::equal(a.begin(), a.end(), b.begin(), [](const PrintPhoto& x, const PrintPhoto& y) {
               return x.kind == y.kind && x.path == y.path;
           });
}

bool photo_record_matches(const nlohmann::json& record, double start_time) {
    if (!record.is_object() || json_util::safe_int(record, "version", 0) != kPhotoRecordVersion) {
        return false;
    }
    const nlohmann::json* start = json_util::find_member(record, "start_time");
    if (start == nullptr || !start->is_number()) {
        return false;
    }
    return std::fabs(json_util::as_double(*start) - start_time) < 1.0;
}

nlohmann::json photo_record_with(const nlohmann::json& record, const std::string& job_id,
                                 const std::string& filename, double start_time,
                                 const std::string& kind, const std::string& path, size_t size,
                                 int64_t taken_at) {
    nlohmann::json out;
    if (photo_record_matches(record, start_time)) {
        out = record;
    } else {
        out = nlohmann::json{{"version", kPhotoRecordVersion},
                             {"job_id", job_id},
                             {"filename", filename},
                             {"start_time", start_time},
                             {"photos", nlohmann::json::array()}};
    }
    nlohmann::json photos = nlohmann::json::array();
    if (const nlohmann::json* list = json_util::find_member(out, "photos");
        list != nullptr && list->is_array()) {
        for (const auto& entry : *list) {
            if (entry.is_object() && json_util::safe_string(entry, "kind", "") != kind) {
                photos.push_back(entry);
            }
        }
    }
    photos.push_back(nlohmann::json{
        {"kind", kind}, {"path", path}, {"size", size}, {"taken_at", taken_at}, {"source", "app"}});
    std::stable_sort(photos.begin(), photos.end(),
                     [](const nlohmann::json& a, const nlohmann::json& b) {
                         return json_util::safe_string(a, "kind", "") == kPhotoKindLayer &&
                                json_util::safe_string(b, "kind", "") != kPhotoKindLayer;
                     });
    out["photos"] = std::move(photos);
    return out;
}

bool photo_record_is_newer(const nlohmann::json& record) {
    const nlohmann::json* version = json_util::find_member(record, "version");
    return version != nullptr && version->is_number_integer() &&
           json_util::as_int(*version) > kPhotoRecordVersion;
}

std::vector<std::string> photo_records_to_prune(const nlohmann::json& records, size_t max_jobs) {
    std::vector<std::string> drop;
    if (!records.is_object()) {
        return drop;
    }
    std::vector<std::pair<double, std::string>> live;
    for (auto it = records.begin(); it != records.end(); ++it) {
        const nlohmann::json& record = it.value();
        if (photo_record_is_newer(record)) {
            continue;
        }
        const nlohmann::json* start =
            record.is_object() ? json_util::find_member(record, "start_time") : nullptr;
        if (json_util::safe_int(record, "version", 0) != kPhotoRecordVersion || start == nullptr ||
            !start->is_number()) {
            drop.push_back(it.key());
            continue;
        }
        live.emplace_back(json_util::as_double(*start), it.key());
    }
    std::sort(live.begin(), live.end());
    for (size_t i = 0; live.size() - i > max_jobs; ++i) {
        drop.push_back(live[i].second);
    }
    return drop;
}

std::vector<std::string> photo_record_paths(const nlohmann::json& record) {
    std::vector<std::string> paths;
    if (!record.is_object() || json_util::safe_int(record, "version", 0) != kPhotoRecordVersion) {
        return paths;
    }
    const nlohmann::json* list = json_util::find_member(record, "photos");
    if (list == nullptr || !list->is_array()) {
        return paths;
    }
    for (const auto& entry : *list) {
        std::string path = json_util::safe_string(entry, "path", "");
        if (is_photo_path(path)) {
            paths.push_back(std::move(path));
        }
    }
    return paths;
}

} // namespace helix
