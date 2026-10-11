// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "utils/decimal_parse.h"

#include <clocale>
#include <string>

#include "../catch_amalgamated.hpp"

using Catch::Approx;
using helix::parse_decimal;

namespace {

float parse(const std::string& s) {
    float value = -1.0f;
    const auto [ptr, ec] = parse_decimal(s.data(), s.data() + s.size(), value);
    REQUIRE(ec == std::errc{});
    REQUIRE(ptr == s.data() + s.size());
    return value;
}

} // namespace

TEST_CASE("parse_decimal - basic grammar", "[decimal_parse]") {
    CHECK(parse("0") == Approx(0.0f));
    CHECK(parse("42") == Approx(42.0f));
    CHECK(parse("42.5") == Approx(42.5f));
    CHECK(parse("-42.5") == Approx(-42.5f));
    CHECK(parse("0.001") == Approx(0.001f));
    CHECK(parse("45.") == Approx(45.0f));
}

TEST_CASE("parse_decimal - no recognizable number", "[decimal_parse]") {
    float value = 7.0f;
    SECTION("empty range") {
        std::string s;
        const auto [ptr, ec] = parse_decimal(s.data(), s.data(), value);
        CHECK(ec == std::errc::invalid_argument);
        CHECK(ptr == s.data());
        CHECK(value == Approx(7.0f)); // unmodified
    }
    SECTION("lone minus") {
        std::string s = "-";
        const auto [ptr, ec] = parse_decimal(s.data(), s.data() + s.size(), value);
        CHECK(ec == std::errc::invalid_argument);
        CHECK(ptr == s.data());
        CHECK(value == Approx(7.0f));
    }
    SECTION("no digits at all") {
        std::string s = "xyz";
        const auto [ptr, ec] = parse_decimal(s.data(), s.data() + s.size(), value);
        CHECK(ec == std::errc::invalid_argument);
        CHECK(ptr == s.data());
    }
}

TEST_CASE("parse_decimal - a leading '+' is rejected, matching std::from_chars<float>",
          "[decimal_parse]") {
    // std::from_chars recognizes only a leading '-'; a '+' is not part of the
    // number grammar at all, unlike strtof/atof.
    std::string s = "+42";
    float value = 7.0f;
    const auto [ptr, ec] = parse_decimal(s.data(), s.data() + s.size(), value);
    CHECK(ec == std::errc::invalid_argument);
    CHECK(ptr == s.data());
    CHECK(value == Approx(7.0f)); // unmodified
}

TEST_CASE("parse_decimal - a lone '+' with no digits is also rejected", "[decimal_parse]") {
    std::string s = "+";
    float value = 7.0f;
    const auto [ptr, ec] = parse_decimal(s.data(), s.data() + s.size(), value);
    CHECK(ec == std::errc::invalid_argument);
    CHECK(ptr == s.data());
}

TEST_CASE("parse_decimal - overflow returns result_out_of_range, value unmodified",
          "[decimal_parse]") {
    std::string huge = std::string(400, '9');
    float value = 3.5f;
    const auto [ptr, ec] = parse_decimal(huge.data(), huge.data() + huge.size(), value);
    CHECK(ec == std::errc::result_out_of_range);
    CHECK(ptr == huge.data() + huge.size()); // still advances past the digits consumed
    CHECK(value == Approx(3.5f));            // left unmodified, unlike strtof's HUGE_VALF
}

TEST_CASE("parse_decimal - exponent notation is not parsed", "[decimal_parse]") {
    // The digit run before 'e' parses; 'e2' is trailing junk like any other
    // non-digit suffix, not scientific notation.
    std::string s = "1e2";
    float value = 0.0f;
    const auto [ptr, ec] = parse_decimal(s.data(), s.data() + s.size(), value);
    CHECK(ec == std::errc{});
    CHECK(ptr == s.data() + 1); // stops after '1'
    CHECK(value == Approx(1.0f));
}

TEST_CASE("parse_decimal - trailing junk still parses the leading number", "[decimal_parse]") {
    std::string s = "45xyz";
    float value = 0.0f;
    const auto [ptr, ec] = parse_decimal(s.data(), s.data() + s.size(), value);
    CHECK(ec == std::errc{});
    CHECK(ptr == s.data() + 2);
    CHECK(value == Approx(45.0f));
}

TEST_CASE("parse_decimal - decimal point is always '.' regardless of the process locale",
          "[decimal_parse]") {
    std::string saved = std::setlocale(LC_NUMERIC, nullptr);
    const char* applied = std::setlocale(LC_NUMERIC, "de_DE.UTF-8");
    if (applied == nullptr) {
        WARN("de_DE.UTF-8 locale not installed; skipping locale-independence check");
    } else {
        CHECK(parse("42.5") == Approx(42.5f));
    }
    std::setlocale(LC_NUMERIC, saved.c_str());
}
