// Copyright (C) 2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "firmware_error_envelope.h"

#include <string>

#include "../catch_amalgamated.hpp"

using helix::decode_firmware_error_text;
using helix::find_firmware_error_envelope;
using helix::FirmwareCodeScheme;

namespace {

// webhooks.state_message on a Snapmaker U1 after an MCU disconnect.
const std::string kU1Shutdown =
    R"({"coded": "0003-0522-0002-0008", "oneshot": 0, "msg":"Lost communication with MCU 'e0'"})"
    "\nOnce the underlying issue is corrected, use the\n"
    "\"FIRMWARE_RESTART\" command to reset the firmware, reload the\n"
    "config, and restart the host software.\n"
    "Printer is shutdown\n";

} // namespace

TEST_CASE("U1 shutdown envelope yields msg, code and Klipper's footer",
          "[firmware_error_envelope]") {
    auto env = find_firmware_error_envelope(kU1Shutdown);
    REQUIRE(env.has_value());
    CHECK(env->code == "0003-0522-0002-0008");
    CHECK(env->scheme == FirmwareCodeScheme::Opaque);
    CHECK(env->msg == "Lost communication with MCU 'e0'");
    CHECK(env->prefix.empty());

    const std::string text = env->text();
    CHECK(text.rfind("Lost communication with MCU 'e0'\nOnce the underlying issue", 0) == 0);
    CHECK(text.find("Printer is shutdown") != std::string::npos);
    CHECK(text.find('{') == std::string::npos);
    CHECK(text.find("coded") == std::string::npos);
}

TEST_CASE("pure error-table envelope", "[firmware_error_envelope]") {
    auto env = find_firmware_error_envelope(
        R"({"code":"key849","msg":"retrude error, failed to exit connections","values":[1,"A"]})");
    REQUIRE(env.has_value());
    CHECK(env->code == "key849");
    CHECK(env->scheme == FirmwareCodeScheme::ErrorTable);
    CHECK(env->values_json == R"([1,"A"])");
    CHECK(env->text() == "retrude error, failed to exit connections");
}

TEST_CASE("embedded envelope keeps the prose prefix, drops Klipper's bang",
          "[firmware_error_envelope]") {
    auto env = find_firmware_error_envelope(
        R"(Internal error during connect: !{"code":"key298","msg":"Can not update MCU rpi config as it is shutdown","values":["rpi"]})");
    REQUIRE(env.has_value());
    CHECK(env->code == "key298");
    CHECK(env->prefix == "Internal error during connect:");
    CHECK(env->text() ==
          "Internal error during connect: Can not update MCU rpi config as it is shutdown");
}

TEST_CASE("text after the object survives verbatim", "[firmware_error_envelope]") {
    CHECK(
        decode_firmware_error_text(
            R"(Error executing script X: {"coded":"0001-0531-0000-0005","msg":"Printer is not idle","action":"none"} (gcode line 42))") ==
        "Error executing script X: Printer is not idle (gcode line 42)");
}

TEST_CASE("whitespace inside the object is tolerated", "[firmware_error_envelope]") {
    auto env =
        find_firmware_error_envelope(R"({ "code" :  "key12" , "msg" : "Heater not heating" })");
    REQUIRE(env.has_value());
    CHECK(env->code == "key12");
    CHECK(env->msg == "Heater not heating");
}

TEST_CASE("braces and quotes inside strings do not end the object", "[firmware_error_envelope]") {
    auto env = find_firmware_error_envelope(
        R"(x {"coded":"0003-0001-0000-0001","msg":"bad {value} \"q\" }"} tail)");
    REQUIRE(env.has_value());
    CHECK(env->msg == R"(bad {value} "q" })");
    CHECK(env->text() == R"(x bad {value} "q" } tail)");
}

TEST_CASE("malformed or unbalanced JSON is left alone", "[firmware_error_envelope]") {
    const std::string broken = R"(Internal error: {"code":"key298, "msg":"broken"})";
    CHECK_FALSE(find_firmware_error_envelope(broken).has_value());
    CHECK(decode_firmware_error_text(broken) == broken);

    const std::string unterminated = R"({"code":"key1", "msg":"No active exception to reraise)";
    CHECK_FALSE(find_firmware_error_envelope(unterminated).has_value());
    CHECK(decode_firmware_error_text(unterminated) == unterminated);
}

TEST_CASE("an envelope without a string msg is left alone", "[firmware_error_envelope]") {
    CHECK_FALSE(find_firmware_error_envelope(R"({"coded":"0001-0531-0000-0005","action":"none"})")
                    .has_value());
    CHECK_FALSE(
        find_firmware_error_envelope(R"({"coded":"0001-0531-0000-0005","msg":42})").has_value());

    // An error-table code is still found, since the table has wording for it,
    // but with no msg there is nothing to put in the object's place.
    const std::string table_only = R"({"code":"key849","values":[1,"B"]})";
    auto env = find_firmware_error_envelope(table_only);
    REQUIRE(env.has_value());
    CHECK(env->code == "key849");
    CHECK(env->msg.empty());
    CHECK(decode_firmware_error_text(table_only) == table_only);
}

TEST_CASE("an object with no known code key is not an envelope", "[firmware_error_envelope]") {
    const std::string other = R"(result {"error":"WebRequestError","msg":"hello"} end)";
    CHECK_FALSE(find_firmware_error_envelope(other).has_value());
    CHECK(decode_firmware_error_text(other) == other);
    // A numeric code is a JSON-RPC error object, not a firmware envelope.
    CHECK_FALSE(find_firmware_error_envelope(R"({"code":-32601,"msg":"x"})").has_value());
}

TEST_CASE("a non-envelope object before the envelope is skipped", "[firmware_error_envelope]") {
    auto env =
        find_firmware_error_envelope(R"({"a":1} then {"code":"key9001","msg":"MCU shutdown"})");
    REQUIRE(env.has_value());
    CHECK(env->code == "key9001");
    CHECK(env->prefix == R"({"a":1} then)");
}

TEST_CASE("a code key whose value is not an error-table code is opaque",
          "[firmware_error_envelope]") {
    auto env = find_firmware_error_envelope(R"({"code":"E0011","msg":"Nozzle clogged"})");
    REQUIRE(env.has_value());
    CHECK(env->code == "E0011");
    CHECK(env->scheme == FirmwareCodeScheme::Opaque);
    CHECK(env->text() == "Nozzle clogged");

    // An opaque code means nothing without its message.
    CHECK_FALSE(find_firmware_error_envelope(R"({"code":"E0011","values":[1]})").has_value());
}

TEST_CASE("an envelope nested in another object is left raw", "[firmware_error_envelope]") {
    const std::string nested = R"(pre {"wrapper": {"coded":"0001-0001","msg":"m"}} post)";
    CHECK_FALSE(find_firmware_error_envelope(nested).has_value());
    CHECK(decode_firmware_error_text(nested) == nested);

    // An envelope after the wrapper is still found, with the wrapper intact.
    auto env = find_firmware_error_envelope(
        R"({"wrapper": {"coded":"0001-0001","msg":"m"}} {"coded":"0002-0002","msg":"n"})");
    REQUIRE(env.has_value());
    CHECK(env->code == "0002-0002");
    CHECK(env->prefix == R"({"wrapper": {"coded":"0001-0001","msg":"m"}})");
}

TEST_CASE("plain prose and empty text pass through", "[firmware_error_envelope]") {
    CHECK(decode_firmware_error_text("Must home axis first") == "Must home axis first");
    CHECK(decode_firmware_error_text("") == "");
    CHECK(decode_firmware_error_text("{}") == "{}");
}
