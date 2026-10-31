// SPDX-License-Identifier: GPL-3.0-or-later
#include "utils/lan_host.h"

#include <arpa/inet.h>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>

#include "../catch_amalgamated.hpp"

using helix::net::is_lan_address;
using helix::net::is_lan_host;

TEST_CASE("is_lan_host accepts private, link-local and unique-local literals",
          "[lan_host][network]") {
    CHECK(is_lan_host("192.168.1.50"));
    CHECK(is_lan_host("10.0.0.7"));
    CHECK(is_lan_host("172.16.0.1"));
    CHECK(is_lan_host("172.31.255.254"));
    CHECK(is_lan_host("169.254.10.20"));
    CHECK(is_lan_host("fe80::1"));
    CHECK(is_lan_host("fe80::1%wlan0"));
    CHECK(is_lan_host("[fd12:3456::7]"));
    CHECK(is_lan_host("::ffff:192.168.0.9"));
}

TEST_CASE("is_lan_host leaves CGNAT, loopback, public and multicast to the default route",
          "[lan_host][network]") {
    // Tailscale assigns 100.64/10, and its traffic belongs on the VPN.
    CHECK_FALSE(is_lan_host("100.64.0.1"));
    CHECK_FALSE(is_lan_host("100.101.102.103"));
    CHECK_FALSE(is_lan_host("127.0.0.1"));
    CHECK_FALSE(is_lan_host("::1"));
    CHECK_FALSE(is_lan_host("0.0.0.0"));
    CHECK_FALSE(is_lan_host("172.32.0.1"));
    CHECK_FALSE(is_lan_host("192.169.1.1"));
    CHECK_FALSE(is_lan_host("8.8.8.8"));
    CHECK_FALSE(is_lan_host("2001:db8::1"));
    CHECK_FALSE(is_lan_host("224.0.0.251"));
    CHECK_FALSE(is_lan_host("ff02::fb"));
}

TEST_CASE("is_lan_host takes .local names and nothing else by name", "[lan_host][network]") {
    CHECK(is_lan_host("voron.local"));
    CHECK(is_lan_host("Voron.LOCAL"));
    CHECK(is_lan_host("voron.local."));
    CHECK_FALSE(is_lan_host(".local"));
    CHECK_FALSE(is_lan_host("local"));
    CHECK_FALSE(is_lan_host("voron.localdomain"));
    CHECK_FALSE(is_lan_host("voron"));
    CHECK_FALSE(is_lan_host("printer.example.com"));
    CHECK_FALSE(is_lan_host("voron.tail1234.ts.net"));
    CHECK_FALSE(is_lan_host(""));
}

TEST_CASE("is_lan_address classifies resolved IPv4 and IPv6 peers", "[lan_host][network]") {
    sockaddr_in v4{};
    v4.sin_family = AF_INET;
    REQUIRE(inet_pton(AF_INET, "192.168.1.50", &v4.sin_addr) == 1);
    CHECK(is_lan_address(reinterpret_cast<const sockaddr*>(&v4)));
    REQUIRE(inet_pton(AF_INET, "100.70.1.2", &v4.sin_addr) == 1);
    CHECK_FALSE(is_lan_address(reinterpret_cast<const sockaddr*>(&v4)));

    sockaddr_in6 v6{};
    v6.sin6_family = AF_INET6;
    REQUIRE(inet_pton(AF_INET6, "fe80::abcd", &v6.sin6_addr) == 1);
    CHECK(is_lan_address(reinterpret_cast<const sockaddr*>(&v6)));
    REQUIRE(inet_pton(AF_INET6, "::ffff:10.1.2.3", &v6.sin6_addr) == 1);
    CHECK(is_lan_address(reinterpret_cast<const sockaddr*>(&v6)));
    REQUIRE(inet_pton(AF_INET6, "2606:4700::1111", &v6.sin6_addr) == 1);
    CHECK_FALSE(is_lan_address(reinterpret_cast<const sockaddr*>(&v6)));

    sockaddr unix_addr{};
    unix_addr.sa_family = AF_UNIX;
    CHECK_FALSE(is_lan_address(&unix_addr));
    CHECK_FALSE(is_lan_address(nullptr));
}
