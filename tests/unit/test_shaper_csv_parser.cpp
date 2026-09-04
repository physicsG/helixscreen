// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_shaper_csv_parser.cpp
 * @brief Unit tests for Klipper shaper CSV parser
 */

#include "../../include/shaper_csv_parser.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "../catch_amalgamated.hpp"

using namespace helix::calibration;
namespace fs = std::filesystem;

// ============================================================================
// Test Helpers
// ============================================================================

namespace {

/// RAII temp file that auto-deletes on destruction
struct TempCsvFile {
    std::string path;

    explicit TempCsvFile(const std::string& content) {
        path = (fs::temp_directory_path() / ("test_shaper_csv_" +
                                             std::to_string(std::hash<std::string>{}(content) ^
                                                            reinterpret_cast<uintptr_t>(this)) +
                                             ".csv"))
                   .string();
        std::ofstream out(path);
        out << content;
    }

    ~TempCsvFile() {
        std::remove(path.c_str());
    }
};

/// Realistic CSV content matching Klipper's calibrate_shaper.py output (real format, no marker)
const char* REALISTIC_CSV =
    "freq, psd_x, psd_y, psd_z, psd_xyz, zv(59.0), mzv(53.8), ei(56.2), 2hump_ei(71.8), "
    "3hump_ei(89.6)\n"
    "5.0, 1.234e-03, 2.345e-03, 1.123e-03, 4.702e-03, 0.001, 0.001, 0.001, 0.000, 0.000\n"
    "10.0, 2.500e-03, 3.100e-03, 1.800e-03, 7.400e-03, 0.002, 0.002, 0.002, 0.001, 0.001\n"
    "15.0, 4.100e-03, 5.200e-03, 2.900e-03, 1.220e-02, 0.004, 0.003, 0.004, 0.002, 0.001\n"
    "20.0, 8.700e-03, 1.020e-02, 5.600e-03, 2.450e-02, 0.009, 0.007, 0.008, 0.004, 0.003\n"
    "25.0, 1.500e-02, 1.800e-02, 9.200e-03, 4.220e-02, 0.016, 0.012, 0.014, 0.008, 0.005\n"
    "30.0, 3.200e-02, 4.100e-02, 2.100e-02, 9.400e-02, 0.035, 0.026, 0.030, 0.017, 0.011\n"
    "35.0, 6.800e-02, 8.500e-02, 4.200e-02, 1.950e-01, 0.074, 0.055, 0.063, 0.036, 0.024\n"
    "40.0, 1.200e-01, 1.500e-01, 7.800e-02, 3.480e-01, 0.130, 0.098, 0.112, 0.065, 0.043\n"
    "45.0, 2.100e-01, 2.800e-01, 1.400e-01, 6.300e-01, 0.228, 0.171, 0.196, 0.113, 0.075\n"
    "50.0, 3.500e-01, 4.200e-01, 2.100e-01, 9.800e-01, 0.380, 0.285, 0.327, 0.189, 0.126\n"
    "55.0, 2.800e-01, 3.600e-01, 1.700e-01, 8.100e-01, 0.304, 0.228, 0.261, 0.151, 0.101\n"
    "60.0, 1.500e-01, 2.000e-01, 9.500e-02, 4.450e-01, 0.163, 0.122, 0.140, 0.081, 0.054\n";

/// CSV without shaper columns (raw PSD only)
const char* RAW_PSD_ONLY_CSV = "freq, psd_x, psd_y, psd_z, psd_xyz\n"
                               "5.0, 1.234e-03, 2.345e-03, 1.123e-03, 4.702e-03\n"
                               "10.0, 2.500e-03, 3.100e-03, 1.800e-03, 7.400e-03\n"
                               "15.0, 4.100e-03, 5.200e-03, 2.900e-03, 1.220e-02\n";

} // anonymous namespace

// ============================================================================
// Tests
// ============================================================================

TEST_CASE("Valid CSV parse with all columns", "[shaper_csv]") {
    TempCsvFile csv(REALISTIC_CSV);
    auto data = parse_shaper_csv(csv.path, 'X');

    SECTION("frequency bins parsed correctly") {
        REQUIRE(data.frequencies.size() == 12);
        CHECK(data.frequencies[0] == Catch::Approx(5.0f));
        CHECK(data.frequencies[5] == Catch::Approx(30.0f));
        CHECK(data.frequencies[11] == Catch::Approx(60.0f));
    }

    SECTION("raw PSD values match psd_x column") {
        REQUIRE(data.raw_psd.size() == 12);
        CHECK(data.raw_psd[0] == Catch::Approx(1.234e-03f));
        CHECK(data.raw_psd[7] == Catch::Approx(1.200e-01f));
        CHECK(data.raw_psd[9] == Catch::Approx(3.500e-01f));
    }

    SECTION("shaper curves count and names") {
        REQUIRE(data.shaper_curves.size() == 5);
        CHECK(data.shaper_curves[0].name == "zv");
        CHECK(data.shaper_curves[1].name == "mzv");
        CHECK(data.shaper_curves[2].name == "ei");
        CHECK(data.shaper_curves[3].name == "2hump_ei");
        CHECK(data.shaper_curves[4].name == "3hump_ei");
    }

    SECTION("shaper fitted frequencies") {
        CHECK(data.shaper_curves[0].frequency == Catch::Approx(59.0f));
        CHECK(data.shaper_curves[1].frequency == Catch::Approx(53.8f));
        CHECK(data.shaper_curves[2].frequency == Catch::Approx(56.2f));
        CHECK(data.shaper_curves[3].frequency == Catch::Approx(71.8f));
        CHECK(data.shaper_curves[4].frequency == Catch::Approx(89.6f));
    }

    SECTION("shaper curve values have correct row count") {
        for (const auto& curve : data.shaper_curves) {
            CHECK(curve.values.size() == 12);
        }
    }

    SECTION("spot-check shaper curve values (transfer function * raw PSD)") {
        // zv at row 0: transfer=0.001, raw_psd=1.234e-03 → 1.234e-06
        CHECK(data.shaper_curves[0].values[0] == Catch::Approx(0.001f * 1.234e-03f));
        // mzv at row 9 (50 Hz): transfer=0.285, raw_psd=0.350 → 0.09975
        CHECK(data.shaper_curves[1].values[9] == Catch::Approx(0.285f * 0.350f));
    }
}

TEST_CASE("X vs Y axis selection", "[shaper_csv]") {
    TempCsvFile csv(REALISTIC_CSV);

    auto data_x = parse_shaper_csv(csv.path, 'X');
    auto data_y = parse_shaper_csv(csv.path, 'Y');

    SECTION("X axis gets psd_x values") {
        REQUIRE(data_x.raw_psd.size() == 12);
        CHECK(data_x.raw_psd[0] == Catch::Approx(1.234e-03f));
    }

    SECTION("Y axis gets psd_y values") {
        REQUIRE(data_y.raw_psd.size() == 12);
        CHECK(data_y.raw_psd[0] == Catch::Approx(2.345e-03f));
    }

    SECTION("X and Y raw_psd differ") {
        CHECK(data_x.raw_psd[0] != Catch::Approx(data_y.raw_psd[0]));
    }

    SECTION("frequencies are identical for both axes") {
        REQUIRE(data_x.frequencies.size() == data_y.frequencies.size());
        for (size_t i = 0; i < data_x.frequencies.size(); ++i) {
            CHECK(data_x.frequencies[i] == Catch::Approx(data_y.frequencies[i]));
        }
    }
}

TEST_CASE("Missing file returns empty data", "[shaper_csv]") {
    auto data = parse_shaper_csv("/tmp/nonexistent_shaper_csv_test_file.csv", 'X');

    CHECK(data.frequencies.empty());
    CHECK(data.raw_psd.empty());
    CHECK(data.shaper_curves.empty());
}

TEST_CASE("Empty file returns empty data", "[shaper_csv]") {
    TempCsvFile csv("");
    auto data = parse_shaper_csv(csv.path, 'X');

    CHECK(data.frequencies.empty());
    CHECK(data.raw_psd.empty());
    CHECK(data.shaper_curves.empty());
}

TEST_CASE("Header only with no data rows", "[shaper_csv]") {
    TempCsvFile csv("freq, psd_x, psd_y, psd_z, psd_xyz, zv(59.0), mzv(53.8)\n");
    auto data = parse_shaper_csv(csv.path, 'X');

    CHECK(data.frequencies.empty());
    CHECK(data.raw_psd.empty());
    // Shaper curves should exist but have no values
    CHECK(data.shaper_curves.size() == 2);
    CHECK(data.shaper_curves[0].name == "zv");
    CHECK(data.shaper_curves[0].values.empty());
}

TEST_CASE("CSV without shaper columns parses raw PSD only", "[shaper_csv]") {
    TempCsvFile csv(RAW_PSD_ONLY_CSV);
    auto data = parse_shaper_csv(csv.path, 'X');

    CHECK(data.frequencies.size() == 3);
    CHECK(data.raw_psd.size() == 3);
    CHECK(data.shaper_curves.empty());

    CHECK(data.frequencies[0] == Catch::Approx(5.0f));
    CHECK(data.raw_psd[0] == Catch::Approx(1.234e-03f));
}

TEST_CASE("Shaper header parsing for complex names", "[shaper_csv]") {
    TempCsvFile csv("freq, psd_x, psd_y, psd_z, psd_xyz, 2hump_ei(71.8), 3hump_ei(89.6)\n"
                    "10.0, 0.001, 0.002, 0.001, 0.004, 0.005, 0.003\n");
    auto data = parse_shaper_csv(csv.path, 'X');

    REQUIRE(data.shaper_curves.size() == 2);

    CHECK(data.shaper_curves[0].name == "2hump_ei");
    CHECK(data.shaper_curves[0].frequency == Catch::Approx(71.8f));
    CHECK(data.shaper_curves[0].values.size() == 1);
    // transfer=0.005, raw_psd=0.001 → 5e-06
    CHECK(data.shaper_curves[0].values[0] == Catch::Approx(0.005f * 0.001f));

    CHECK(data.shaper_curves[1].name == "3hump_ei");
    CHECK(data.shaper_curves[1].frequency == Catch::Approx(89.6f));
    CHECK(data.shaper_curves[1].values.size() == 1);
    // transfer=0.003, raw_psd=0.001 → 3e-06
    CHECK(data.shaper_curves[1].values[0] == Catch::Approx(0.003f * 0.001f));
}

TEST_CASE("Parser detects shaper columns without marker", "[shaper_csv]") {
    // Real Klipper format has no shapers: marker - shaper columns follow psd_xyz directly
    TempCsvFile csv("freq,psd_x,psd_y,psd_z,psd_xyz,zv(59.6),mzv(55.0),ei(67.2)\n"
                    "5.0,0.500,0.600,0.300,1.400,0.123,0.456,0.789\n"
                    "10.0,1.000,0.800,0.500,2.300,0.100,0.200,0.300\n");
    auto data = parse_shaper_csv(csv.path, 'X');

    REQUIRE(data.shaper_curves.size() == 3);
    CHECK(data.shaper_curves[0].name == "zv");
    CHECK(data.shaper_curves[0].frequency == Catch::Approx(59.6f));
    CHECK(data.shaper_curves[1].name == "mzv");
    CHECK(data.shaper_curves[1].frequency == Catch::Approx(55.0f));
    CHECK(data.shaper_curves[2].name == "ei");
    CHECK(data.shaper_curves[2].frequency == Catch::Approx(67.2f));

    // Verify shaper values = transfer_function * raw_psd
    REQUIRE(data.shaper_curves[0].values.size() == 2);
    CHECK(data.shaper_curves[0].values[0] == Catch::Approx(0.123f * 0.500f)); // zv row 0
    CHECK(data.shaper_curves[0].values[1] == Catch::Approx(0.100f * 1.000f)); // zv row 1
    CHECK(data.shaper_curves[2].values[0] == Catch::Approx(0.789f * 0.500f)); // ei row 0
}
