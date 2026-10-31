// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_moonraker_connect_failure.cpp
 * @brief A never-opened connection reports why it failed, from the real socket.
 *
 * The setup wizard tells a refused port apart from a host that never answered.
 * That only works if MoonrakerClient reads the cause off the closing socket
 * before libhv closes its fd, so these drive real loopback sockets.
 */

#include "../../include/moonraker_client.h"
#include "hv/EventLoopThread.h"

#include <arpa/inet.h>
#include <atomic>
#include <chrono>
#include <memory>
#include <netinet/in.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

#include "../catch_amalgamated.hpp"

using namespace helix;

namespace {

/// A loopback socket on an ephemeral port. Listening, it takes connections.
/// Not listening, it releases the port again: a port nothing is bound to is
/// refused at once on every OS, while a socket bound without listen() is
/// refused by Linux but silently dropped by macOS. Another process could claim
/// the freed port before the test connects; on loopback that window is tiny.
class LoopbackListener {
  public:
    explicit LoopbackListener(bool listening) {
        fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        REQUIRE(fd_ >= 0);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;
        REQUIRE(::bind(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);
        if (listening) {
            REQUIRE(::listen(fd_, 8) == 0);
        }
        socklen_t len = sizeof(addr);
        REQUIRE(::getsockname(fd_, reinterpret_cast<sockaddr*>(&addr), &len) == 0);
        port_ = ntohs(addr.sin_port);
        if (!listening) {
            close_listener();
        }
    }
    ~LoopbackListener() {
        close_listener();
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    /// Accept connections and drop them without answering the upgrade.
    void accept_and_drop() {
        thread_ = std::thread([this]() {
            while (true) {
                int c = ::accept(fd_, nullptr, nullptr);
                if (c < 0) {
                    return;
                }
                ::close(c);
            }
        });
    }

    void close_listener() {
        if (fd_ >= 0) {
            ::shutdown(fd_, SHUT_RDWR);
            ::close(fd_);
            fd_ = -1;
        }
    }

    std::string url() const {
        return "ws://127.0.0.1:" + std::to_string(port_) + "/websocket";
    }

  private:
    int fd_{-1};
    uint16_t port_{0};
    std::thread thread_;
};

/// Connect once with reconnect off and wait for the attempt's on_disconnected.
ConnectFailureKind failure_after_one_attempt(const std::string& url) {
    auto loop = std::make_shared<hv::EventLoopThread>();
    loop->start();
    auto client = std::make_unique<MoonrakerClient>(loop->loop());
    client->set_initial_connect_failure_timeout(60000);

    std::atomic<bool> closed{false};
    client->connect(url.c_str(), []() {}, [&closed]() { closed.store(true); });
    client->set_auto_reconnect(false);

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!closed.load() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    REQUIRE(closed.load());
    const ConnectFailureKind kind = client->last_connect_failure();

    client->disconnect();
    loop->stop();
    loop->join();
    client.reset();
    return kind;
}

} // namespace

TEST_CASE("A connection to a closed port reports Refused",
          "[moonraker][client][connect_failure][eventloop][slow]") {
    LoopbackListener bound_only(/*listening=*/false);

    CHECK(failure_after_one_attempt(bound_only.url()) == ConnectFailureKind::Refused);
}

TEST_CASE("A peer that accepts TCP and drops the upgrade is not called refused",
          "[moonraker][client][connect_failure][eventloop][slow]") {
    LoopbackListener listener(/*listening=*/true);
    listener.accept_and_drop();

    CHECK(failure_after_one_attempt(listener.url()) == ConnectFailureKind::Unknown);
}

TEST_CASE("A new attempt does not report the previous attempt's failure",
          "[moonraker][client][connect_failure][eventloop][slow]") {
    LoopbackListener refusing(/*listening=*/false);
    // Listens but never accepts: the TCP handshake completes from the backlog
    // and the upgrade waits unanswered, so the attempt stays in flight.
    LoopbackListener silent(/*listening=*/true);

    auto loop = std::make_shared<hv::EventLoopThread>();
    loop->start();
    auto client = std::make_unique<MoonrakerClient>(loop->loop());
    client->set_initial_connect_failure_timeout(60000);

    std::atomic<bool> closed{false};
    client->connect(refusing.url().c_str(), []() {}, [&closed]() { closed.store(true); });
    client->set_auto_reconnect(false);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!closed.load() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    REQUIRE(closed.load());
    REQUIRE(client->last_connect_failure() == ConnectFailureKind::Refused);

    client->connect(silent.url().c_str(), []() {}, []() {});
    client->set_auto_reconnect(false);
    CHECK(client->last_connect_failure() == ConnectFailureKind::Unknown);

    client->disconnect();
    loop->stop();
    loop->join();
    client.reset();
}
