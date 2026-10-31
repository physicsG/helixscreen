// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "hv/HttpMessage.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

/// Certificate verification for the app's own internet endpoints.
///
/// Only requests to our servers verify: the update manifest, release downloads and
/// changelog, telemetry, crash reports and debug bundle uploads. They go through
/// trusted_request() / trusted_download(). Every other libhv request (Moonraker,
/// cameras, Spoolman, IPP, plugins, anything the user typed) keeps libhv's unverified
/// default, because self-signed certificates are normal on printer LANs.
namespace helix::tls {

/// Where the CA certificates live. Both empty means no store was found.
struct CaStore {
    std::string file;
    std::string dir;
    bool empty() const {
        return file.empty() && dir.empty();
    }
};

/// Resolves the CA store: a readable $SSL_CERT_FILE / $SSL_CERT_DIR first, then the
/// non-empty `bundled_file`, then the first readable entry of `system_files`.
CaStore find_ca_store(const std::vector<std::string>& system_files,
                      const std::string& bundled_file);

/// find_ca_store() over the certs/ca-certificates.crt shipped next to the install,
/// then the usual system bundle paths.
CaStore find_ca_store();

/// A client SSL_CTX (as libhv's hssl_ctx_t) that verifies the server's chain against
/// `store` and its name against the host being connected to. Never null when OpenSSL
/// is built in: a store that fails to load leaves the context with no trust anchors,
/// so every handshake fails instead of passing unverified. Null without OpenSSL.
void* make_client_ctx(const CaStore& store);

/// The stage a request or download failed at.
enum class FailureKind {
    None,
    Http,         ///< a response arrived with a status other than 200
    TlsCert,      ///< the chain or the host name failed verification
    TlsHandshake, ///< the TLS handshake failed for any other reason
    Dns,          ///< the host name did not resolve
    Connect,      ///< the TCP connection was refused or unreachable
    Timeout,      ///< the connection or the whole request ran out of time
    Transfer,     ///< the connection failed after the request was sent
    NoResponse,   ///< the connection closed before a status line arrived
    ShortBody,    ///< the body ended before its Content-Length
    EmptyBody,    ///< a 200 response with no body at all
    Redirect,     ///< a redirect that downgrades to http, or too many of them
    FileOpen,     ///< the staging file could not be created
    FileWrite,    ///< writing the staging file failed (ENOSPC, EIO)
    FileRename,   ///< the finished file could not be moved into place
};

/// A fixed lowercase token for `kind` ("tls_cert", "short_body"); empty for None.
const char* failure_kind_name(FailureKind kind);

/// Why a trusted_request() or trusted_download() came back empty.
struct TransferFailure {
    FailureKind kind = FailureKind::None;
    /// Status of the last response, -1 when none arrived.
    int http_status = -1;
    /// TlsCert: the X509_V_ERR_* code. Connect, Transfer, File*: the errno.
    /// Otherwise libhv's return code, or 0.
    int code = 0;
    /// Host of the request that failed, after any redirects. Raw: may name any host.
    std::string host;
};

/// requests::request() for one of our own endpoints: the server certificate is
/// verified, and redirects are followed on the same verifying context. Null on any
/// transport or verification failure, with the reason in `failure` when given. With
/// no CA store on the device it logs one warning and sends unverified.
HttpResponsePtr trusted_request(const HttpRequestPtr& req, TransferFailure* failure = nullptr);

/// requests::downloadFile() over trusted_request(). 0 on failure, with the reason in
/// `failure` when given.
size_t trusted_download(const std::string& url, const std::string& path,
                        const std::function<void(size_t received, size_t total)>& progress,
                        TransferFailure* failure = nullptr);

namespace detail {
/// `location` from a redirect response, resolved against the URL that returned it.
std::string resolve_location(const std::string& current_url, const std::string& location);

/// What the verifying context saw of one send's TLS handshake.
struct SendTrace {
    bool handshake_started = false;
    bool handshake_done = false;
    int cert_error = 0; ///< X509_V_ERR_* the verify callback rejected with, 0 if none
};

/// The stage a failed hv::HttpClient::send() stopped at, from its return code `rc`
/// (negative for a connect-stage failure, positive for one after it), what the
/// handshake trace saw, and whether the host resolves now.
TransferFailure classify_send_failure(int rc, const SendTrace& trace, bool host_resolves);

/// trusted_request() with an explicit context, for tests.
HttpResponsePtr trusted_request(const HttpRequestPtr& req, void* ssl_ctx,
                                TransferFailure* failure = nullptr);

/// trusted_download() with an explicit context, for tests. @p retry_delay_ms is
/// how long libhv waits before re-sending a request whose connection dropped.
size_t trusted_download(const std::string& url, const std::string& path,
                        const std::function<void(size_t received, size_t total)>& progress,
                        void* ssl_ctx, TransferFailure* failure,
                        uint32_t retry_delay_ms = DEFAULT_HTTP_FAIL_RETRY_DELAY);
} // namespace detail

} // namespace helix::tls
