// SPDX-License-Identifier: GPL-3.0-or-later
#include "snapmaker_resume.h"

#include "hv/json.hpp"

namespace helix {

namespace {
using json = nlohmann::json;
} // namespace

std::vector<TerminalMatcher> snapmaker_terminal_matchers() {
    // Exception 532 is the defect_detection family: code 1 dirty bed, 2 noodle,
    // 3 residue, 4 dirty nozzle. The firmware PAUSEs (does not cancel) for all
    // four, so only the hardware-verified dirty-bed case (#991) is Terminal —
    // the rest attempt RESUME. Match by message substring OR by id+code so
    // either signal alone suffices. NOT gated on sdcard-inactive: runout
    // (id 523) also deactivates the SD, so sdcard state cannot discriminate
    // terminal-vs-recoverable here.
    return {
        TerminalMatcher{/*message_substr=*/"dirty bed", /*exception_id=*/-1,
                        /*require_sdcard_inactive=*/false},
        TerminalMatcher{/*message_substr=*/"", /*exception_id=*/532,
                        /*require_sdcard_inactive=*/false, /*exception_code=*/1},
    };
}

std::string snapmaker_extract_coded_msg(const std::string& raw_error, const std::string& fallback) {
    size_t start = raw_error.find('{');
    if (start == std::string::npos) {
        return fallback;
    }

    // Scan forward from the first '{' tracking brace depth (respecting
    // quoted strings and escapes) to find the matching closing brace of the
    // embedded JSON object — the payload may be embedded inside a longer
    // gcode error string, so substr(first '{', last '}') isn't safe.
    int depth = 0;
    bool in_string = false;
    bool escaped = false;
    size_t end = std::string::npos;
    for (size_t i = start; i < raw_error.size(); ++i) {
        char c = raw_error[i];
        if (in_string) {
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                in_string = false;
            }
            continue;
        }
        if (c == '"') {
            in_string = true;
        } else if (c == '{') {
            ++depth;
        } else if (c == '}') {
            --depth;
            if (depth == 0) {
                end = i;
                break;
            }
        }
    }
    if (end == std::string::npos) {
        return fallback;
    }

    std::string candidate = raw_error.substr(start, end - start + 1);
    json parsed = json::parse(candidate, nullptr, false);
    if (parsed.is_discarded()) {
        return fallback;
    }
    if (!parsed.is_object()) {
        return fallback;
    }
    auto msg_it = parsed.find("msg");
    if (msg_it == parsed.end() || !msg_it->is_string()) {
        return fallback;
    }
    return msg_it->get<std::string>();
}

} // namespace helix
