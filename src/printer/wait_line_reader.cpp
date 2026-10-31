// SPDX-License-Identifier: GPL-3.0-or-later
#include "wait_line_reader.h"

#include "i_moonraker_sub_apis.h"
#include "json_utils.h"

namespace helix {

std::optional<uint64_t> virtual_sdcard_file_position(const json& query_response) {
    const json* result = json_util::find_member(query_response, "result");
    const json* status = json_util::find_member(result ? *result : query_response, "status");
    if (!status) {
        status = &query_response; // a notify_status_update status object
    }
    const json* sdcard = status ? json_util::find_member(*status, "virtual_sdcard") : nullptr;
    const json* position = sdcard ? json_util::find_member(*sdcard, "file_position") : nullptr;
    uint64_t pos = 0;
    if (!position || !position->is_number() || !json_util::detail::to_u64(*position, pos)) {
        return std::nullopt;
    }
    return pos;
}

void read_wait_line_at(ITransfersAPI& transfers, const std::string& filename,
                       uint64_t file_position, std::function<void(const WaitLine&)> on_line,
                       std::function<void(const MoonrakerError&)> on_error) {
    transfers.download_file_range(
        "gcodes", filename, file_position, kWaitLineWindow,
        [on_line = std::move(on_line)](const std::string& bytes) {
            const std::optional<std::string_view> line =
                first_line_at_position(bytes, kWaitLineWindow);
            on_line(line ? classify_wait_line(*line) : WaitLine{});
        },
        std::move(on_error));
}

} // namespace helix
