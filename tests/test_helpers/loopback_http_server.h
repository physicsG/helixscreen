// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "hv/HttpServer.h"

#include <arpa/inet.h>
#include <chrono>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

namespace helix::test {

/// A loopback port nothing is listening on. Asked of the kernel per fixture so
/// parallel test shards (and other sessions' runs) never contend for one number.
inline int free_loopback_port() {
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    socklen_t len = sizeof(addr);
    const bool ok = fd >= 0 && ::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0 &&
                    ::getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len) == 0;
    if (fd >= 0)
        ::close(fd);
    return ok ? ntohs(addr.sin_port) : 0;
}

/// True once a TCP connect to the loopback port succeeds.
inline bool accepts_connections(int port) {
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(static_cast<uint16_t>(port));
    const bool ok = fd >= 0 && ::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0;
    if (fd >= 0)
        ::close(fd);
    return ok;
}

/// A libhv HTTP server on a free loopback port, for the life of the object.
/// Register routes on service() before start().
class LoopbackHttpServer {
  public:
    LoopbackHttpServer() = default;
    LoopbackHttpServer(const LoopbackHttpServer&) = delete;
    LoopbackHttpServer& operator=(const LoopbackHttpServer&) = delete;

    ~LoopbackHttpServer() {
        server_.stop();
    }

    hv::HttpService& service() {
        return service_;
    }

    /// Start serving and wait until the listener accepts. False if it never does.
    bool start() {
        server_.registerHttpService(&service_);
        server_.setPort(port_);
        // One loop thread: every case sends one request at a time, and libhv's
        // HttpMessage caches its Date header in a process-wide buffer that two loop
        // threads write without a lock.
        server_.setThreadNum(1);
        started_ = port_ != 0 && server_.start() == 0;
        // start() is asynchronous: probe until the listener accepts.
        for (int i = 0; started_ && i < 500 && !accepts_connections(port_); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return started_;
    }

    [[nodiscard]] bool started() const {
        return started_;
    }

    std::string url(const std::string& path) const {
        return "http://127.0.0.1:" + std::to_string(port_) + path;
    }

  private:
    const int port_ = free_loopback_port();
    hv::HttpService service_;
    hv::HttpServer server_;
    bool started_ = false;
};

} // namespace helix::test
