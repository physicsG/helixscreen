// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// A one-request HTTP/1.0 client over a plain loopback socket, for tests of the
// app's own HTTP servers. libhv's requests:: client starts event-loop threads that
// outlive a test; this one is a connect, a send and a read to EOF.

#include <arpa/inet.h>
#include <cctype>
#include <cstdlib>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
#include <utility>
#include <vector>

#include "../catch_amalgamated.hpp"

namespace helix::test {

struct RawHttpReply {
    int status = 0;
    std::string headers; ///< Lower-cased status line and header block.
    std::string body;

    bool has_header(const std::string& lower_name_and_value) const {
        return headers.find(lower_name_and_value) != std::string::npos;
    }
};

inline std::string raw_http_lower(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

inline RawHttpReply raw_http(int port, const std::string& method, const std::string& path,
                             const std::vector<std::pair<std::string, std::string>>& headers = {},
                             const std::string& body = "") {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    REQUIRE(fd >= 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(static_cast<uint16_t>(port));
    REQUIRE(::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);

    std::string req =
        method + " " + path + " HTTP/1.0\r\nHost: 127.0.0.1:" + std::to_string(port) + "\r\n";
    for (const auto& [name, value] : headers) {
        req += name + ": " + value + "\r\n";
    }
    if (!body.empty()) {
        req += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    }
    req += "\r\n" + body;
    REQUIRE(::send(fd, req.data(), req.size(), 0) == static_cast<ssize_t>(req.size()));

    std::string raw;
    char buf[4096];
    ssize_t n;
    while ((n = ::recv(fd, buf, sizeof(buf), 0)) > 0) {
        raw.append(buf, static_cast<size_t>(n));
    }
    ::close(fd);

    const size_t split = raw.find("\r\n\r\n");
    REQUIRE(split != std::string::npos);
    RawHttpReply reply;
    reply.status = std::atoi(raw.c_str() + 9); // past "HTTP/1.x "
    reply.headers = raw_http_lower(raw.substr(0, split));
    reply.body = raw.substr(split + 4);
    return reply;
}

} // namespace helix::test
