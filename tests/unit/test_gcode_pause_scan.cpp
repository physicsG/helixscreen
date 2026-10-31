// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gcode_layer_index.h"
#include "gcode_pause_scan.h"
#include "test_helpers/unique_temp_dir.h"

#include <clocale>
#include <fstream>
#include <sstream>
#include <unistd.h>

#include "../catch_amalgamated.hpp"

using namespace helix::gcode;
using Catch::Approx;

// Helper to create a temporary G-code file (same pattern as
// test_gcode_layer_index.cpp).
class TempPauseScanGCode {
  public:
    explicit TempPauseScanGCode(const std::string& content) {
        path_ = helix::test::unique_temp_file("test_pause_scan", "gcode");
        std::ofstream file(path_);
        file << content;
        file.close();
    }

    ~TempPauseScanGCode() {
        std::remove(path_.c_str());
    }

    const std::string& path() const {
        return path_;
    }

  private:
    std::string path_;
};

TEST_CASE("PauseScan - command classification", "[gcode][pause_scan]") {
    SECTION("filament change M600") {
        REQUIRE(classify_pause_command("M600") == PauseKind::FilamentChange);
        REQUIRE(classify_pause_command("M600 P1 T0") == PauseKind::FilamentChange);
        REQUIRE(classify_pause_command("  m600") == PauseKind::FilamentChange);
    }
    SECTION("macro pause spellings") {
        REQUIRE(classify_pause_command("PAUSE") == PauseKind::Macro);
        REQUIRE(classify_pause_command("M601") == PauseKind::Macro);
        REQUIRE(classify_pause_command("pause  ; user asked to pause") == PauseKind::Macro);
        REQUIRE(classify_pause_command(" M601 ") == PauseKind::Macro);
    }
    SECTION("stop spelling M0") {
        REQUIRE(classify_pause_command("M0") == PauseKind::Stop);
        REQUIRE(classify_pause_command("M0 hot end cooling") == PauseKind::Stop);
    }
    SECTION("comments are not commands") {
        REQUIRE_FALSE(classify_pause_command("; M600"));
        REQUIRE_FALSE(classify_pause_command(";M600 filament change"));
        REQUIRE_FALSE(classify_pause_command("G1 X10 E5 ; PAUSE"));
    }
    SECTION("word boundary rejects longer tokens") {
        REQUIRE_FALSE(classify_pause_command("M6000"));
        REQUIRE_FALSE(classify_pause_command("PAUSED"));
        REQUIRE_FALSE(classify_pause_command("PAUSE_NODE"));
        REQUIRE_FALSE(classify_pause_command("M6011"));
        // A trailing '\r' (CRLF read in binary mode) terminates the word.
        REQUIRE(classify_pause_command("M600\r") == PauseKind::FilamentChange);
    }
    SECTION("other commands are not pauses") {
        REQUIRE_FALSE(classify_pause_command("G1 X10 Y10 E1"));
        REQUIRE_FALSE(classify_pause_command("M73 P50 R12"));
        REQUIRE_FALSE(classify_pause_command("M117 PAUSE NOW"));
        REQUIRE_FALSE(classify_pause_command("T0"));
        REQUIRE_FALSE(classify_pause_command(""));
        REQUIRE_FALSE(classify_pause_command("   "));
    }
}

TEST_CASE("PauseScan - M73 progress extraction", "[gcode][pause_scan]") {
    SECTION("P value with and without R") {
        REQUIRE(m73_progress_percent("M73 P50 R12") == Approx(50.0f));
        REQUIRE(m73_progress_percent("M73 P0") == Approx(0.0f));
        REQUIRE(m73_progress_percent("M73 P42.5") == Approx(42.5f));
        REQUIRE(m73_progress_percent("m73 p7") == Approx(7.0f));
    }
    SECTION("Bambu Q (fine bar) does not shadow P") {
        REQUIRE(m73_progress_percent("M73 P30 Q31 R55") == Approx(30.0f));
    }
    SECTION("no P parameter") {
        REQUIRE_FALSE(m73_progress_percent("M73 R12").has_value());
        REQUIRE_FALSE(m73_progress_percent("M117 M73 P50").has_value());
    }
    SECTION("P must start a token") {
        // "XP50" is one token; the P is embedded, not a parameter.
        REQUIRE_FALSE(m73_progress_percent("M73 XP50").has_value());
    }
    SECTION("commented M73 is not live progress") {
        REQUIRE_FALSE(m73_progress_percent("; M73 P50").has_value());
    }
    SECTION("bare P with no digits is not a parameter") {
        REQUIRE_FALSE(m73_progress_percent("M73 P").has_value());
        REQUIRE_FALSE(m73_progress_percent("M73 P R10").has_value());
        REQUIRE_FALSE(m73_progress_percent("M73 Pxyz").has_value());
    }
    SECTION("a lone sign with no digits after it is not a parameter") {
        REQUIRE_FALSE(m73_progress_percent("M73 P-").has_value());
        REQUIRE_FALSE(m73_progress_percent("M73 P+").has_value());
    }
    SECTION("a leading + is rejected, matching std::from_chars<float>") {
        // std::from_chars recognizes only a leading '-'; unlike strtof/atof, a
        // '+' is not part of the number grammar at all.
        REQUIRE_FALSE(m73_progress_percent("M73 P+42").has_value());
    }
    SECTION("a value too large for float is rejected, matching std::from_chars<float>'s "
            "result_out_of_range") {
        std::string huge_line = "M73 P" + std::string(400, '9');
        REQUIRE_FALSE(m73_progress_percent(huge_line).has_value());
    }
    SECTION("exponent notation is not parsed") {
        // Controller ruling: no slicer emits M73 P in scientific notation, so
        // this reads only the leading digits and treats 'e2' as trailing junk,
        // the same as any other non-digit suffix -- std::from_chars would read
        // the whole "1e2" as 100.
        REQUIRE(m73_progress_percent("M73 P1e2") == Approx(1.0f));
    }
    SECTION("trailing junk after digits still parses the leading number") {
        REQUIRE(m73_progress_percent("M73 P45xyz") == Approx(45.0f));
    }
    SECTION("comma is not a decimal separator") {
        // Only '.' introduces a fraction; a comma stops the digit run the same
        // way any other non-digit trailing character does.
        REQUIRE(m73_progress_percent("M73 P42,5") == Approx(42.0f));
    }
    SECTION("negative P clamps to 0") {
        REQUIRE(m73_progress_percent("M73 P-10") == Approx(0.0f));
    }
    SECTION("P above 100 clamps to 100") {
        REQUIRE(m73_progress_percent("M73 P150") == Approx(100.0f));
    }
    SECTION("a trailing decimal point with no fraction digits still parses") {
        REQUIRE(m73_progress_percent("M73 P45.") == Approx(45.0f));
    }
    SECTION("decimal point is always '.' regardless of the process locale") {
        // std::from_chars is locale-independent by standard; the portable
        // replacement must keep that property rather than falling back to a
        // locale-sensitive C parse (strtof/atof honor LC_NUMERIC's comma).
        std::string saved = std::setlocale(LC_NUMERIC, nullptr);
        const char* applied = std::setlocale(LC_NUMERIC, "de_DE.UTF-8");
        if (applied == nullptr) {
            WARN("de_DE.UTF-8 locale not installed; skipping locale-independence check");
        } else {
            REQUIRE(m73_progress_percent("M73 P42.5") == Approx(42.5f));
        }
        std::setlocale(LC_NUMERIC, saved.c_str());
    }
}

TEST_CASE("PauseScan - collects pauses with both axis fractions", "[gcode][pause_scan]") {
    // Hand-built lines with byte offsets counted by hand (each includes '\n').
    std::string l0 = "M73 P0 R60\n";  // offset 0,   11 bytes
    std::string l1 = "G1 X10 E1\n";   // offset 11,  10 bytes
    std::string l2 = "M73 P25 R45\n"; // offset 21,  12 bytes
    std::string l3 = "M600\n";        // offset 33,   5 bytes
    std::string l4 = "G1 X20 E2\n";   // offset 38,  10 bytes
    std::string l5 = "PAUSE\n";       // offset 48,   6 bytes
    std::string l6 = "M73 P75 R15\n"; // offset 54,  12 bytes
    std::string l7 = "M0\n";          // offset 66,   3 bytes
    const size_t total = 69;

    PauseScan scan;
    scan.begin(total);
    uint64_t off = 0;
    for (const std::string* line : {&l0, &l1, &l2, &l3, &l4, &l5, &l6, &l7}) {
        std::string_view v(*line);
        v.remove_suffix(1); // feed getline-style: no '\n'
        scan.feed_line(v, off, 0);
        off += line->size();
    }

    SECTION("three pauses found with kinds") {
        REQUIRE(scan.pauses().size() == 3);
        REQUIRE(scan.pauses()[0].kind == PauseKind::FilamentChange);
        REQUIRE(scan.pauses()[1].kind == PauseKind::Macro);
        REQUIRE(scan.pauses()[2].kind == PauseKind::Stop);
    }
    SECTION("byte fractions come from offsets") {
        REQUIRE(scan.pauses()[0].file_offset == 33);
        REQUIRE(scan.pauses()[1].file_offset == 48);
        REQUIRE(scan.pauses()[2].file_offset == 66);
        REQUIRE(scan.pauses()[0].byte_fraction == Approx(33.0f / total));
        REQUIRE(scan.pauses()[1].byte_fraction == Approx(48.0f / total));
        REQUIRE(scan.pauses()[2].byte_fraction == Approx(66.0f / total));
    }
    SECTION("slicer fractions come from the last M73 at or before the pause") {
        REQUIRE(scan.pauses()[0].slicer_fraction == Approx(0.25f));
        REQUIRE(scan.pauses()[1].slicer_fraction == Approx(0.25f));
        REQUIRE(scan.pauses()[2].slicer_fraction == Approx(0.75f));
    }
    SECTION("file with M73 lines fills the slicer-time axis") {
        REQUIRE(scan.has_m73());
        REQUIRE(scan.axis() == ProgressAxis::SlicerTime);
    }
}

TEST_CASE("PauseScan - no M73 means byte-position axis", "[gcode][pause_scan]") {
    PauseScan scan;
    scan.begin(100);
    scan.feed_line("G1 X10 E1", 0, 0);
    scan.feed_line("M601", 50, 3);
    scan.feed_line("G1 X20 E2", 54, 3);

    REQUIRE_FALSE(scan.has_m73());
    REQUIRE(scan.axis() == ProgressAxis::BytePosition);
    REQUIRE(scan.pauses().size() == 1);
    REQUIRE(scan.pauses()[0].byte_fraction == Approx(0.5f));
    // Before any M73, the slicer coordinate of a pause is the start of the job.
    REQUIRE(scan.pauses()[0].slicer_fraction == Approx(0.0f));
}

TEST_CASE("PauseScan - display fraction follows the axis", "[gcode][pause_scan]") {
    ScheduledPause p{};
    p.byte_fraction = 0.6f;
    p.slicer_fraction = 0.4f;

    SECTION("byte-position file uses byte fraction") {
        REQUIRE(display_fraction(p, ProgressAxis::BytePosition) == Approx(0.6f));
    }
    SECTION("slicer-time file uses the M73 fraction") {
        REQUIRE(display_fraction(p, ProgressAxis::SlicerTime) == Approx(0.4f));
    }
}

TEST_CASE("GCodeLayerIndex - scheduled pauses ride the layer scan",
          "[gcode][pause_scan][layer_index]") {
    // Two layers, an M600 after the first M73, a PAUSE before any M73 in the
    // second half, in a file with no M73 at all after the midpoint.
    std::string gcode = R"(M73 P10 R90
G1 Z0.2 F1000
G1 X10 Y10 E1
M600
G1 Z0.4 F1000
G1 X20 Y20 E2
M73 P60 R40
PAUSE
G1 X30 Y30 E3
)";

    TempPauseScanGCode file(gcode);
    GCodeLayerIndex index;
    REQUIRE(index.build_from_file(file.path()));

    const auto& stats = index.get_stats();
    REQUIRE(stats.pause_scan->pauses.size() == 2);
    REQUIRE(stats.pause_scan->axis == ProgressAxis::SlicerTime);

    const ScheduledPause& first = stats.pause_scan->pauses[0];
    REQUIRE(first.kind == PauseKind::FilamentChange);
    REQUIRE(first.slicer_fraction == Approx(0.10f));
    REQUIRE(first.byte_fraction ==
            Approx(static_cast<float>(first.file_offset) / static_cast<float>(stats.total_bytes)));
    REQUIRE(first.file_offset > 0);

    const ScheduledPause& second = stats.pause_scan->pauses[1];
    REQUIRE(second.kind == PauseKind::Macro);
    REQUIRE(second.slicer_fraction == Approx(0.60f));
    REQUIRE(second.layer_index > first.layer_index);
}

TEST_CASE("GCodeLayerIndex - pause-free file collects nothing", "[gcode][pause_scan]") {
    std::string gcode = R"(G1 Z0.2 F1000
G1 X10 Y10 E1
G1 Z0.4 F1000
G1 X20 Y20 E2
)";

    TempPauseScanGCode file(gcode);
    GCodeLayerIndex index;
    REQUIRE(index.build_from_file(file.path()));

    REQUIRE(index.get_stats().pause_scan->pauses.empty());
    REQUIRE(index.get_stats().pause_scan->axis == ProgressAxis::BytePosition);
}

TEST_CASE("GCodeLayerIndex - a pause before the first layer has layer -1",
          "[gcode][pause_scan][layer_index]") {
    std::string gcode = R"(M601
G1 Z0.2 F1000
G1 X10 Y10 E1
)";

    TempPauseScanGCode file(gcode);
    GCodeLayerIndex index;
    REQUIRE(index.build_from_file(file.path()));

    REQUIRE(index.get_stats().pause_scan->pauses.size() == 1);
    REQUIRE(index.get_stats().pause_scan->pauses[0].layer_index == -1);
}

TEST_CASE("PauseScan - reported layer extraction", "[gcode][pause_scan][layer_pause]") {
    CHECK(reported_current_layer("SET_PRINT_STATS_INFO CURRENT_LAYER=12") == 12);
    CHECK(reported_current_layer("set_print_stats_info current_layer=3 ; c") == 3);
    CHECK(reported_current_layer("SET_PRINT_STATS_INFO TOTAL_LAYER=240 CURRENT_LAYER=0") == 0);
    CHECK(reported_current_layer("  SET_PRINT_STATS_INFO CURRENT_LAYER=7\r") == 7);
    // No CURRENT_LAYER, a commented one, a malformed value, another command.
    CHECK_FALSE(reported_current_layer("SET_PRINT_STATS_INFO TOTAL_LAYER=240").has_value());
    CHECK_FALSE(reported_current_layer("SET_PRINT_STATS_INFO ; CURRENT_LAYER=4").has_value());
    CHECK_FALSE(reported_current_layer("SET_PRINT_STATS_INFO CURRENT_LAYER=").has_value());
    CHECK_FALSE(reported_current_layer("SET_PRINT_STATS_INFO CURRENT_LAYER=4x").has_value());
    CHECK_FALSE(reported_current_layer("SET_PRINT_STATS_INFOX CURRENT_LAYER=4").has_value());
    CHECK_FALSE(reported_current_layer("; SET_PRINT_STATS_INFO CURRENT_LAYER=4").has_value());
    CHECK_FALSE(reported_current_layer("M117 SET_PRINT_STATS_INFO CURRENT_LAYER=4").has_value());
}

TEST_CASE("PauseScan - layer marks carry both axis fractions", "[gcode][pause_scan][layer_pause]") {
    PauseScan scan;
    scan.begin(1000);
    scan.feed_line("SET_PRINT_STATS_INFO TOTAL_LAYER=3 CURRENT_LAYER=0", 0, -1);
    scan.feed_line("SET_PRINT_STATS_INFO CURRENT_LAYER=1", 100, 0);
    scan.feed_line("M73 P40 R10", 200, 0);
    scan.feed_line("SET_PRINT_STATS_INFO CURRENT_LAYER=2", 400, 1);
    // A repeat of the same layer keeps the first mark only.
    scan.feed_line("SET_PRINT_STATS_INFO CURRENT_LAYER=2", 450, 1);
    scan.feed_line("SET_PRINT_STATS_INFO CURRENT_LAYER=3", 800, 2);

    const auto result = scan.result();
    REQUIRE(result.layer_marks.size() == 4);
    CHECK(result.axis == ProgressAxis::SlicerTime);
    CHECK(result.pauses.empty());

    const LayerMark* two = find_layer_mark(result, 2);
    REQUIRE(two != nullptr);
    CHECK(two->byte_fraction == Approx(0.40f));
    CHECK(two->slicer_fraction == Approx(0.40f));
    const LayerMark* one = find_layer_mark(result, 1);
    REQUIRE(one != nullptr);
    CHECK(one->byte_fraction == Approx(0.10f));
    CHECK(one->slicer_fraction == Approx(0.0f));
    CHECK(display_fraction(*one, ProgressAxis::BytePosition) == Approx(0.10f));
    CHECK(display_fraction(*one, ProgressAxis::SlicerTime) == Approx(0.0f));
    CHECK(find_layer_mark(result, 9) == nullptr);

    // A new pass starts empty.
    scan.begin(10);
    CHECK(scan.layer_marks().empty());
}

TEST_CASE("GCodeLayerIndex - layer marks ride the layer scan", "[gcode][pause_scan][layer_pause]") {
    std::string gcode = R"(SET_PRINT_STATS_INFO TOTAL_LAYER=2
;LAYER_CHANGE
SET_PRINT_STATS_INFO CURRENT_LAYER=1
G1 Z0.2 F1000
G1 X10 Y10 E1
;LAYER_CHANGE
SET_PRINT_STATS_INFO CURRENT_LAYER=2
G1 Z0.4 F1000
G1 X20 Y20 E2
)";

    TempPauseScanGCode file(gcode);
    GCodeLayerIndex index;
    REQUIRE(index.build_from_file(file.path()));
    REQUIRE(index.get_stats().pause_scan != nullptr);
    const auto& scan = *index.get_stats().pause_scan;
    REQUIRE(scan.layer_marks.size() == 2);
    CHECK(scan.layer_marks[0].layer == 1);
    CHECK(scan.layer_marks[1].layer == 2);
    CHECK(scan.layer_marks[1].byte_fraction > scan.layer_marks[0].byte_fraction);
}

TEST_CASE("PauseScan - a carriage return ends the layer scan",
          "[gcode][pause_scan][layer_pause][1703]") {
    CHECK_FALSE(reported_current_layer("SET_PRINT_STATS_INFO TOTAL_LAYER=50\r").has_value());
    CHECK_FALSE(reported_current_layer("SET_PRINT_STATS_INFO TOTAL_LAYER=50\r\n").has_value());
    CHECK_FALSE(reported_current_layer("SET_PRINT_STATS_INFO\r").has_value());
    CHECK(reported_current_layer("SET_PRINT_STATS_INFO CURRENT_LAYER=3\r") == 3);
    CHECK(reported_current_layer("SET_PRINT_STATS_INFO CURRENT_LAYER=3\r\n") == 3);
    CHECK(reported_current_layer("SET_PRINT_STATS_INFO TOTAL_LAYER=50\r CURRENT_LAYER=4") == 4);

    PauseScan scan;
    scan.begin(200);
    scan.feed_line("SET_PRINT_STATS_INFO TOTAL_LAYER=2\r", 0, -1);
    scan.feed_line("SET_PRINT_STATS_INFO CURRENT_LAYER=1\r", 40, 0);
    scan.feed_line("M600\r", 80, 0);
    scan.feed_line("SET_PRINT_STATS_INFO CURRENT_LAYER=2\r", 100, 1);
    REQUIRE(scan.layer_marks().size() == 2);
    CHECK(scan.layer_marks()[0].layer == 1);
    CHECK(scan.layer_marks()[1].layer == 2);
    CHECK(scan.pauses().size() == 1);
}

TEST_CASE("GCodeLayerIndex - a CRLF file still records layer marks",
          "[gcode][pause_scan][layer_pause][1703]") {
    std::string gcode = "SET_PRINT_STATS_INFO TOTAL_LAYER=2\r\n"
                        ";LAYER_CHANGE\r\n"
                        "SET_PRINT_STATS_INFO CURRENT_LAYER=1\r\n"
                        "G1 Z0.2 F1000\r\n"
                        "G1 X10 Y10 E1\r\n"
                        ";LAYER_CHANGE\r\n"
                        "SET_PRINT_STATS_INFO CURRENT_LAYER=2\r\n"
                        "G1 Z0.4 F1000\r\n"
                        "G1 X20 Y20 E2\r\n";

    TempPauseScanGCode file(gcode);
    GCodeLayerIndex index;
    REQUIRE(index.build_from_file(file.path()));
    REQUIRE(index.get_stats().pause_scan != nullptr);
    const auto& scan = *index.get_stats().pause_scan;
    REQUIRE(scan.layer_marks.size() == 2);
    CHECK(scan.layer_marks[0].layer == 1);
    CHECK(scan.layer_marks[1].layer == 2);
}
