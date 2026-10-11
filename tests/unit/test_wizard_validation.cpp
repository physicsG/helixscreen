// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#include "utils/network_validation.h"

#include "../catch_amalgamated.hpp"

// ============================================================================
// IP Address Validation Tests
// ============================================================================

TEST_CASE("IP validation: Valid IPv4 addresses", "[wizard][validation][ip]") {
    REQUIRE(is_valid_ip_or_hostname("192.168.1.1") == true);
    REQUIRE(is_valid_ip_or_hostname("10.0.0.1") == true);
    REQUIRE(is_valid_ip_or_hostname("172.16.0.1") == true);
    REQUIRE(is_valid_ip_or_hostname("127.0.0.1") == true);
    REQUIRE(is_valid_ip_or_hostname("255.255.255.255") == true);
    REQUIRE(is_valid_ip_or_hostname("0.0.0.0") == true);
}

TEST_CASE("IP validation: Invalid IPv4 addresses", "[wizard][validation][ip]") {
    REQUIRE(is_valid_ip_or_hostname("999.1.1.1") == false);     // Out of range
    REQUIRE(is_valid_ip_or_hostname("192.168.1.256") == false); // Last octet > 255
    REQUIRE(is_valid_ip_or_hostname("192.168.1") == false);     // Missing octet
    REQUIRE(is_valid_ip_or_hostname("192.168.1.1.1") == false); // Too many octets
    REQUIRE(is_valid_ip_or_hostname("192.168..1") == false);    // Empty octet
    REQUIRE(is_valid_ip_or_hostname("192.168.1.") == false);    // Trailing dot
    REQUIRE(is_valid_ip_or_hostname(".192.168.1.1") == false);  // Leading dot
}

TEST_CASE("IP validation: Valid hostnames", "[wizard][validation][hostname]") {
    REQUIRE(is_valid_ip_or_hostname("printer") == true);
    REQUIRE(is_valid_ip_or_hostname("printer.local") == true);
    REQUIRE(is_valid_ip_or_hostname("my-printer") == true);
    REQUIRE(is_valid_ip_or_hostname("my_printer") == true);
    REQUIRE(is_valid_ip_or_hostname("PRINTER123") == true);
    REQUIRE(is_valid_ip_or_hostname("voron-2.4") == true);
    REQUIRE(is_valid_ip_or_hostname("k1.local") == true);
    REQUIRE(is_valid_ip_or_hostname("192.168.1.1a") ==
            true); // Valid hostname (looks like IP + letter)
}

TEST_CASE("IP validation: Invalid hostnames", "[wizard][validation][hostname]") {
    REQUIRE(is_valid_ip_or_hostname("") == false);         // Empty
    REQUIRE(is_valid_ip_or_hostname("-printer") == false); // Starts with hyphen
    REQUIRE(is_valid_ip_or_hostname("!invalid") == false); // Invalid character
    REQUIRE(is_valid_ip_or_hostname("print@r") == false);  // Invalid character
    REQUIRE(is_valid_ip_or_hostname("print er") == false); // Space
}

// ============================================================================
// Port Validation Tests
// ============================================================================

TEST_CASE("Port validation: Valid ports", "[wizard][validation][port]") {
    REQUIRE(is_valid_port("1") == true);     // Minimum valid
    REQUIRE(is_valid_port("80") == true);    // HTTP
    REQUIRE(is_valid_port("443") == true);   // HTTPS
    REQUIRE(is_valid_port("7125") == true);  // Moonraker default
    REQUIRE(is_valid_port("8080") == true);  // Common alt HTTP
    REQUIRE(is_valid_port("65535") == true); // Maximum valid
}

TEST_CASE("Port validation: Invalid ports", "[wizard][validation][port]") {
    REQUIRE(is_valid_port("") == false);      // Empty
    REQUIRE(is_valid_port("0") == false);     // Zero not allowed
    REQUIRE(is_valid_port("65536") == false); // Above max
    REQUIRE(is_valid_port("99999") == false); // Way above max
    REQUIRE(is_valid_port("-1") == false);    // Negative
    REQUIRE(is_valid_port("abc") == false);   // Non-numeric
    REQUIRE(is_valid_port("12.34") == false); // Decimal
    REQUIRE(is_valid_port("80a") == false);   // Mixed
    REQUIRE(is_valid_port(" 80") == true);    // Leading space (trimmed)
    REQUIRE(is_valid_port("80 ") == true);    // Trailing space (trimmed)
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST_CASE("IP validation: Edge cases", "[wizard][validation][edge]") {
    REQUIRE(is_valid_ip_or_hostname("localhost") == true);   // Common hostname
    REQUIRE(is_valid_ip_or_hostname("raspberrypi") == true); // Common Pi hostname
    REQUIRE(is_valid_ip_or_hostname("mainsailos") == true);  // Common OS
}

TEST_CASE("Port validation: Edge cases", "[wizard][validation][edge]") {
    REQUIRE(is_valid_port("1") == true);     // Minimum
    REQUIRE(is_valid_port("65535") == true); // Maximum
}
