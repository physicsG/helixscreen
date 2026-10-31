// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "http_listen_socket.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace helix {

int bind_tcp_listener(const char* host, int port, int* out_port) {
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    if (host == nullptr || ::inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
        return -1;
    }

    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }
    int reuse = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 ||
        ::listen(fd, 16) != 0) {
        ::close(fd);
        return -1;
    }

    sockaddr_in bound{};
    socklen_t len = sizeof(bound);
    if (::getsockname(fd, reinterpret_cast<sockaddr*>(&bound), &len) != 0) {
        ::close(fd);
        return -1;
    }
    if (out_port != nullptr) {
        *out_port = ntohs(bound.sin_port);
    }
    return fd;
}

} // namespace helix
