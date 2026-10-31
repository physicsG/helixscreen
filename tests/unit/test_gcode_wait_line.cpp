// SPDX-License-Identifier: GPL-3.0-or-later

#include "../test_helpers/archive_transfers.h"
#include "gcode_wait_line.h"
#include "moonraker_client_mock.h"
#include "wait_line_reader.h"

#include <optional>
#include <string>

#include "../catch_amalgamated.hpp"

using helix::classify_wait_line;
using helix::first_line_at_position;
using helix::kWaitLineWindow;
using helix::WaitKind;
using helix::WaitLine;

namespace {

/// The bytes around the waiting line in a Snapmaker U1 print file, and the
/// virtual_sdcard.file_position Klipper reports while that line runs: the
/// start of the line, since the position advances only once it returns.
const std::string kU1File = "G28\nM106 S0\n"
                            "TEMPERATURE_WAIT SENSOR=\"heater_generic dragonbreath\" MINIMUM=35 "
                            "; comment\n"
                            "G90\n";
const size_t kU1Position = kU1File.find("TEMPERATURE_WAIT");

/// What a reader at @p pos sees: the window at pos, classified.
WaitLine classify_window_at(const std::string& file, size_t pos, size_t window = kWaitLineWindow) {
    const std::string bytes = pos < file.size() ? file.substr(pos, window) : std::string();
    const auto line = first_line_at_position(bytes, window);
    return line ? classify_wait_line(*line) : WaitLine{};
}

} // namespace

TEST_CASE("the waiting line is the one that starts at the print position", "[wait_line]") {
    const WaitLine line = classify_window_at(kU1File, kU1Position);
    CHECK(line.kind == WaitKind::TemperatureWait);
    CHECK(line.sensor == "heater_generic dragonbreath");
    REQUIRE(line.minimum);
    CHECK(*line.minimum == Catch::Approx(35));
    CHECK_FALSE(line.maximum);

    // The line before the position is the one that already returned.
    CHECK(classify_window_at(kU1File, kU1File.find("M106")).kind == WaitKind::Other);
}

TEST_CASE("the first line at the position ends at its newline, or at the file's end",
          "[wait_line]") {
    CHECK(first_line_at_position("M190 S100\nG28\n", 256) == "M190 S100");
    CHECK(first_line_at_position("M190 S100\r\nG28\n", 256) == "M190 S100\r");
    // Fewer bytes than asked: the file ended, so its last line is whole.
    CHECK(first_line_at_position("M109 S200", 256) == "M109 S200");
    // No bytes: the position is at or past the end.
    CHECK_FALSE(first_line_at_position("", 256));
    CHECK(classify_window_at(kU1File, kU1File.size()).kind == WaitKind::Other);
    // A blank line at the position is a line, and no wait.
    CHECK(first_line_at_position("\nM190 S100\n", 256) == "");
}

TEST_CASE("a line longer than the window is read only when its command is whole", "[wait_line]") {
    const std::string sensor = "SENSOR=\"heater_generic " + std::string(300, 'x') + "\"";
    const std::string long_line = "TEMPERATURE_WAIT " + sensor + " MINIMUM=35\nG90\n";
    // The window filled before the newline: the command may be cut short.
    CHECK_FALSE(first_line_at_position(long_line.substr(0, 256), 256));
    CHECK(classify_window_at(long_line, 0).kind == WaitKind::Other);
    // A comment started inside the window: everything before it is the command.
    const std::string commented = "M191 S60 ;" + std::string(400, 'c') + "\n";
    const WaitLine m191 = classify_window_at(commented, 0);
    CHECK(m191.kind == WaitKind::M191);
    CHECK(m191.target.value_or(0) == Catch::Approx(60));
}

TEST_CASE("heater waits name their heater and temperature", "[wait_line]") {
    SECTION("M109 with a tool") {
        const WaitLine l = classify_wait_line("M109 T1 S250");
        CHECK(l.kind == WaitKind::M109);
        CHECK(l.tool == std::optional<int>(1));
        CHECK(l.target.value_or(0) == Catch::Approx(250));
    }
    SECTION("M109 with no tool waits on the active one") {
        const WaitLine l = classify_wait_line("M109 S200");
        CHECK(l.kind == WaitKind::M109);
        CHECK_FALSE(l.tool);
        CHECK(l.target.value_or(0) == Catch::Approx(200));
    }
    SECTION("M109 R is no target: Klipper waits on S alone") {
        const WaitLine l = classify_wait_line("M109 R215 T0");
        CHECK(l.kind == WaitKind::M109);
        CHECK(l.tool == std::optional<int>(0));
        CHECK_FALSE(l.target);
    }
    SECTION("M190") {
        const WaitLine l = classify_wait_line("M190 S100");
        CHECK(l.kind == WaitKind::M190);
        CHECK(l.target.value_or(0) == Catch::Approx(100));
        CHECK_FALSE(l.tool);
    }
    SECTION("M191") {
        const WaitLine l = classify_wait_line("M191 S60");
        CHECK(l.kind == WaitKind::M191);
        CHECK(l.target.value_or(0) == Catch::Approx(60));
        CHECK(l.sensor.empty());
    }
    SECTION("lower case, leading whitespace, a trailing comment") {
        const WaitLine l = classify_wait_line("  \tm191 s45 ; chamber T3\r");
        CHECK(l.kind == WaitKind::M191);
        CHECK(l.target.value_or(0) == Catch::Approx(45));
        CHECK_FALSE(l.tool); // a T in the comment is not a parameter
    }
    SECTION("no temperature") {
        const WaitLine l = classify_wait_line("M190");
        CHECK(l.kind == WaitKind::M190);
        CHECK_FALSE(l.target);
    }
}

TEST_CASE("TEMPERATURE_WAIT names its sensor and bounds", "[wait_line]") {
    SECTION("MINIMUM on a quoted sensor, lower-cased") {
        const WaitLine l = classify_wait_line(
            "TEMPERATURE_WAIT SENSOR=\"heater_generic DragonBreath\" MINIMUM=35");
        CHECK(l.kind == WaitKind::TemperatureWait);
        CHECK(l.sensor == "heater_generic dragonbreath");
        CHECK(l.minimum.value_or(0) == Catch::Approx(35));
        CHECK_FALSE(l.maximum);
        CHECK_FALSE(l.tool);
        CHECK_FALSE(l.target);
    }
    SECTION("MAXIMUM only") {
        const WaitLine l = classify_wait_line("TEMPERATURE_WAIT SENSOR=chamber MAXIMUM=80");
        CHECK(l.kind == WaitKind::TemperatureWait);
        CHECK(l.sensor == "chamber");
        CHECK_FALSE(l.minimum);
        CHECK(l.maximum.value_or(0) == Catch::Approx(80));
    }
    SECTION("both bounds, lower case") {
        const WaitLine l =
            classify_wait_line("temperature_wait sensor=temperature_sensor minimum=30 maximum=50");
        CHECK(l.kind == WaitKind::TemperatureWait);
        CHECK(l.sensor == "temperature_sensor");
        CHECK(l.minimum.value_or(0) == Catch::Approx(30));
        CHECK(l.maximum.value_or(0) == Catch::Approx(50));
    }
    SECTION("an extruder sensor names its tool") {
        CHECK(classify_wait_line("TEMPERATURE_WAIT SENSOR=extruder MINIMUM=200").tool ==
              std::optional<int>(0));
        CHECK(classify_wait_line("TEMPERATURE_WAIT SENSOR=extruder2 MINIMUM=200").tool ==
              std::optional<int>(2));
        CHECK_FALSE(classify_wait_line("TEMPERATURE_WAIT SENSOR=heater_bed MINIMUM=60").tool);
        CHECK_FALSE(
            classify_wait_line("TEMPERATURE_WAIT SENSOR=\"extruder_stepper x\" MINIMUM=60").tool);
    }
}

TEST_CASE("lines that are no heater wait are Other", "[wait_line]") {
    for (const char* text : {"", "   ", "; just a comment", "  ;M109 S200",
                             "PRINT_START BED=100 CHAMBER=60", "M1910 S5", "M10 S5", "M104 S200",
                             "M140 S60", "TEMPERATURE_WAITX SENSOR=a MINIMUM=1", "G28"}) {
        INFO("line=" << text);
        CHECK(classify_wait_line(text).kind == WaitKind::Other);
    }
}

TEST_CASE("the file position comes from a virtual_sdcard query", "[wait_line]") {
    using helix::virtual_sdcard_file_position;
    CHECK(virtual_sdcard_file_position(
              {{"result", {{"status", {{"virtual_sdcard", {{"file_position", 7342}}}}}}}}) ==
          std::optional<uint64_t>(7342));
    CHECK(
        virtual_sdcard_file_position({{"status", {{"virtual_sdcard", {{"file_position", 5}}}}}}) ==
        std::optional<uint64_t>(5));
    CHECK_FALSE(virtual_sdcard_file_position({{"result", {{"status", json::object()}}}}));
    CHECK_FALSE(virtual_sdcard_file_position(
        {{"result", {{"status", {{"virtual_sdcard", {{"file_position", nullptr}}}}}}}}));
    CHECK_FALSE(virtual_sdcard_file_position(json::array()));
}

TEST_CASE("the reader fetches the window at the position and classifies its first line",
          "[wait_line]") {
    MoonrakerClientMock client{MoonrakerClientMock::PrinterType::VORON_24};
    ArchiveTransfers transfers{client, ""};
    transfers.put("u1.gcode", kU1File);
    std::optional<WaitLine> got;
    bool failed = false;
    auto read = [&](const std::string& file, uint64_t pos) {
        got.reset();
        failed = false;
        helix::read_wait_line_at(
            transfers, file, pos, [&](const WaitLine& l) { got = l; },
            [&](const MoonrakerError&) { failed = true; });
    };

    read("u1.gcode", kU1Position);
    REQUIRE(got);
    CHECK(got->kind == WaitKind::TemperatureWait);
    CHECK(got->sensor == "heater_generic dragonbreath");
    CHECK(transfers.range_bytes <= kWaitLineWindow);

    // The mock answers a range past the end with no bytes, which reads as no
    // wait. Moonraker answers it with 416, which reaches on_error instead.
    read("u1.gcode", kU1File.size());
    REQUIRE(got);
    CHECK(got->kind == WaitKind::Other);

    transfers.fail_ranges = true;
    read("u1.gcode", kU1Position);
    CHECK(failed);
    CHECK_FALSE(got);
}

TEST_CASE("a heater target line names its heater and target, waiting or not",
          "[wait_line][plugin_fitcheck]") {
    using helix::heater_target_line;
    CHECK(heater_target_line("M104 S215").heater == "extruder");
    CHECK(heater_target_line("m109 s230 ; wait").target == std::optional<double>(230.0));
    CHECK(heater_target_line("M104 T1 S200").heater == "extruder1");
    CHECK(heater_target_line("M140 S60").heater == "heater_bed");
    CHECK(heater_target_line("M190 S65").target == std::optional<double>(65.0));
    const auto bed = heater_target_line("SET_HEATER_TEMPERATURE HEATER=heater_bed TARGET=70");
    CHECK(bed.heater == "heater_bed");
    CHECK(bed.target == std::optional<double>(70.0));
    const auto quoted = heater_target_line("SET_HEATER_TEMPERATURE HEATER=\"Extruder\" TARGET=240");
    CHECK(quoted.heater == "extruder");
    CHECK(quoted.target == std::optional<double>(240.0));
    CHECK_FALSE(heater_target_line("M104").target.has_value());
    CHECK(heater_target_line("G1 X10 S500").heater.empty());
    CHECK(heater_target_line("; M104 S999").heater.empty());
}
