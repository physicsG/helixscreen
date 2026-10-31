// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <string>
#include <string_view>

namespace helix {

/// Largest camera snapshot accepted through the platform TLS bridge. A JPEG
/// frame is well under this even at 4K; the cap bounds a misbehaving server.
inline constexpr size_t kMaxPlatformSnapshotBytes = 8 * 1024 * 1024;

/**
 * @brief Whether a request to @p url must use the platform's own TLS stack.
 *
 * True for an https URL when the HTTP client has no TLS of its own: libhv on
 * Android is built without SSL, so an https request through it never leaves the
 * device. Pass libhv's `HV_WITH_SSL` as @p http_client_has_tls.
 */
[[nodiscard]] inline bool needs_platform_tls(const std::string& url, bool http_client_has_tls) {
    if (http_client_has_tls) {
        return false;
    }
    constexpr std::string_view kHttps = "https://";
    return url.size() >= kHttps.size() &&
           std::equal(kHttps.begin(), kHttps.end(), url.begin(), [](char a, char b) {
               return a == std::tolower(static_cast<unsigned char>(b));
           });
}

} // namespace helix
