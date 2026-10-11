// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <utility>

/// POSTs to our own ingest workers (telemetry, crash reports, debug bundles).
/// Owns the API key, the user agent and the transport: certificate-verified libhv
/// everywhere, Android's HttpURLConnection on Android, where libhv has no TLS.
namespace helix::ingest {

/// Shared by every ingest worker. Not a secret: it ships in every binary and only
/// filters drive-by traffic. To rotate: change it here, run
/// `wrangler secret put INGEST_API_KEY` in each worker under server/, and release.
inline constexpr const char* API_KEY = "hx-tel-v1-a7f3c9e2d1b84056";

/// POSTs `body` as application/json, with `content_encoding` (e.g. "gzip") when
/// non-empty. Returns {status, response body}; status 0 is a transport or
/// verification failure.
std::pair<int, std::string> post(const std::string& url, const std::string& body, int timeout_sec,
                                 const std::string& content_encoding = "");

} // namespace helix::ingest
