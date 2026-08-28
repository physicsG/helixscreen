// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Stub layer for the WASM app build.
//
// Every symbol here belongs to a subsystem the browser build deliberately does
// not compile -- the Linux WiFi control socket and libhv's network interface
// enumeration. Nothing on the AMS pages calls any of it; these exist so the
// backends that reference them link. The list was written from what wasm-ld
// reported missing, not guessed.

#include "hv/ifconfig.h"
#include "wifi_saved_config.h"

#include <string>
#include <vector>

// --- libhv ifconfig (hv/ifconfig.h): getifaddrs() over BSD sockets -----------
// Emscripten has no interface list to enumerate. Both ethernet backends probe
// it; an empty list reads as "no interfaces", which is true of a browser tab.
int ifconfig(std::vector<ifconfig_t>& ifcs) {
    ifcs.clear();
    return 0;
}

// --- saved WiFi config (wifi_saved_config.cpp: struct statfs) ----------------
namespace helix::wifi::store {
std::vector<SavedNetwork> load() {
    return {};
}
bool remove(const std::string& /*ssid*/) {
    return false;
}
} // namespace helix::wifi::store

// =============================================================================
// libhv — the synchronous HTTP client and its logger.
//
// The browser build links no libhv: the whole network layer sits behind
// IMoonrakerClient / IMoonrakerAPI, and the AMS pages are driven from a backend
// directly, so nothing here is ever CALLED. These definitions exist purely so
// the TUs that reference them (telemetry, the update checker, Spoolman's HTTP
// probe) resolve. Each one fails closed: a send reports an error, the logger is
// null, so a caller that somehow got here degrades instead of corrupting state.
//
// If a future harness needs real HTTP, it should go through the browser's own
// fetch() behind IMoonrakerClient — not by porting libhv's event loop.
// =============================================================================

#include "hv/HttpClient.h"
#include "hv/HttpMessage.h"
#include "hv/hlog.h"
#include "hv/hstring.h"

namespace hv {
std::string empty_string;
} // namespace hv

// Each class needs its full virtual set, not just the constructor: defining a
// ctor emits a reference to the vtable, and the vtable is only emitted where the
// key function lives.
HttpMessage::HttpMessage() {}
HttpMessage::~HttpMessage() {}
void HttpMessage::Reset() {}
std::string HttpMessage::Dump(bool, bool) {
    return {};
}
std::string HttpMessage::GetHeader(const char* /*key*/, const std::string& defvalue) {
    return defvalue;
}

HttpRequest::HttpRequest() : HttpMessage() {
    type = HTTP_REQUEST;
}
void HttpRequest::Reset() {}
std::string HttpRequest::Dump(bool, bool) {
    return {};
}

HttpResponse::HttpResponse() : HttpMessage() {
    type = HTTP_RESPONSE;
}
void HttpResponse::Reset() {}
std::string HttpResponse::Dump(bool, bool) {
    return {};
}

int http_client_send(HttpRequest* /*req*/, HttpResponse* /*resp*/) {
    return -1; // "no transport" — every caller treats non-zero as failure
}

logger_t* hv_default_logger() {
    return nullptr;
}
void logger_set_level(logger_t* /*logger*/, int /*level*/) {}

#include "hv/hfile.h"
size_t hv_filesize(const char* /*filepath*/) {
    return 0; // update_checker probes a download that never happens here
}
