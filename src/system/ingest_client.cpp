// SPDX-License-Identifier: GPL-3.0-or-later

#include "system/ingest_client.h"

#include "helix_version.h"

#ifdef __ANDROID__
#include "system/http_android.h"

#include <vector>
#else
#include "system/tls_trust.h"
#endif

namespace helix::ingest {

std::pair<int, std::string> post(const std::string& url, const std::string& body, int timeout_sec,
                                 const std::string& content_encoding) {
#ifdef __ANDROID__
    const std::vector<unsigned char> bytes(body.begin(), body.end());
    return helix::android::https_post_binary(url, bytes, "application/json", content_encoding,
                                             HELIX_USER_AGENT, API_KEY, timeout_sec);
#else
    auto req = std::make_shared<HttpRequest>();
    req->method = HTTP_POST;
    req->url = url;
    req->timeout = timeout_sec;
    req->headers["Content-Type"] = "application/json";
    if (!content_encoding.empty()) {
        req->headers["Content-Encoding"] = content_encoding;
    }
    req->headers["User-Agent"] = HELIX_USER_AGENT;
    req->headers["X-API-Key"] = API_KEY;
    req->body = body;

    auto resp = helix::tls::trusted_request(req);
    if (!resp) {
        return {0, ""};
    }
    return {static_cast<int>(resp->status_code), resp->body};
#endif
}

} // namespace helix::ingest
