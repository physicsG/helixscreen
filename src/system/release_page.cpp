// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "system/release_page.h"

namespace helix {

namespace {

constexpr const char* kReleasesUrl = "https://github.com/prestonbrown/helixscreen/releases";

/// The version reaches us from a remote manifest and lands in a URL path, so
/// only the characters a release tag uses are accepted.
bool is_tag_safe(const std::string& version) {
    if (version.empty()) {
        return false;
    }
    for (char c : version) {
        const bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
                        (c >= 'A' && c <= 'Z') || c == '.' || c == '-' || c == '+' || c == '_';
        if (!ok) {
            return false;
        }
    }
    return true;
}

} // namespace

std::string release_page_url(const OfferedRelease& offered) {
    const std::string latest = std::string(kReleasesUrl) + "/latest";
    if (!offered.on_github) {
        return latest;
    }

    std::string tag = offered.tag_name;
    if (tag.empty()) {
        std::string version = offered.version;
        if (!version.empty() && (version[0] == 'v' || version[0] == 'V')) {
            version.erase(0, 1);
        }
        if (!version.empty()) {
            tag = "v" + version;
        }
    }
    if (!is_tag_safe(tag)) {
        return latest;
    }
    return std::string(kReleasesUrl) + "/tag/" + tag;
}

} // namespace helix
