// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_accel_csv_download.cpp
 * @brief Tests for accelerometer CSV download parsing and mock data_store responses
 *
 * Validates:
 * 1. File list response parsing extracts result array correctly
 * 2. Mock data_store path returns accelerometer CSV files
 * 3. Mock server.files.get_file returns CSV content
 * 4. End-to-end flow: list files -> find best match -> download CSV
 */

#include "../../include/moonraker_advanced_api.h"
#include "../../include/moonraker_api.h"
#include "../../include/moonraker_client_mock.h"
#include "../../include/printer_state.h"

#include <string>

#include "../catch_amalgamated.hpp"
#include "hv/json.hpp"

using namespace helix;
using json = nlohmann::json;

// ============================================================================
// 1. File list response parsing - result extraction
// ============================================================================

TEST_CASE("File list response with result wrapper is parsed correctly", "[accel_csv][parsing]") {
    // Simulate the JSON-RPC response format: {"result": [{...}, ...]}
    json response = {{"result", json::array({
                                    {{"path", "raw_data_belt_path_a-20260310_120000.csv"},
                                     {"size", 2048},
                                     {"modified", 1773158400.0}},
                                    {{"path", "raw_data_belt_path_b-20260310_120001.csv"},
                                     {"size", 2048},
                                     {"modified", 1773158401.0}},
                                    {{"path", "raw_data_x-20260310_115000.csv"},
                                     {"size", 4096},
                                     {"modified", 1773154800.0}},
                                })}};

    SECTION("Extract result array and find matching files") {
        REQUIRE(response.contains("result"));
        const auto& result = response["result"];
        REQUIRE(result.is_array());
        REQUIRE(result.size() == 3);

        // Search for belt_path_a files (mimicking download_accel_csv logic)
        std::string target_prefix = "raw_data_belt_path_a";
        std::string best_file;
        for (const auto& file : result) {
            std::string filename = file.value("path", "");
            if (filename.find(target_prefix) != std::string::npos &&
                filename.find(".csv") != std::string::npos) {
                if (filename > best_file) {
                    best_file = filename;
                }
            }
        }
        REQUIRE(best_file == "raw_data_belt_path_a-20260310_120000.csv");
    }

    SECTION("Iterating response directly without result extraction fails to find files") {
        // The old buggy code did: for (const auto& file : response)
        // Confirm the top-level is an object with one key, NOT an array of file entries
        REQUIRE(response.is_object());
        REQUIRE_FALSE(response.is_array());
    }
}

TEST_CASE("File list response without result field is handled", "[accel_csv][parsing]") {
    json empty_response = json::object();
    REQUIRE_FALSE(empty_response.contains("result"));

    json null_result = {{"result", nullptr}};
    REQUIRE(null_result.contains("result"));
    REQUIRE_FALSE(null_result["result"].is_array());
}

// ============================================================================
// 2. CSV content response parsing - result extraction
// ============================================================================

TEST_CASE("CSV content response extracts string from result", "[accel_csv][parsing]") {
    std::string expected_csv = "#time,accel_x,accel_y,accel_z\n"
                               "0.000000,0.1,0.2,9.8\n"
                               "0.001000,0.3,0.5,9.7\n";

    SECTION("Result-wrapped string response") {
        json response = {{"result", expected_csv}};
        REQUIRE(response.contains("result"));
        REQUIRE(response["result"].is_string());
        std::string csv_data = response["result"].get<std::string>();
        REQUIRE(csv_data == expected_csv);
    }

    SECTION("Bare string response (fallback)") {
        json response = expected_csv;
        REQUIRE(response.is_string());
        std::string csv_data = response.get<std::string>();
        REQUIRE(csv_data == expected_csv);
    }
}

// ============================================================================
// 3. Mock data_store file list
// ============================================================================

TEST_CASE("Mock returns data_store accelerometer files", "[accel_csv][mock]") {
    PrinterState state;
    state.init_subjects(false);
    MoonrakerClientMock client(MoonrakerClientMock::PrinterType::GENERIC_COREXY);
    MoonrakerAPI api(client, state);

    bool callback_called = false;
    json captured_response;

    // Request file list for data_store path under config root
    json params;
    params["path"] = "data_store";
    params["root"] = "config";

    client.send_jsonrpc(
        "server.files.list", params,
        [&](const json& response) {
            callback_called = true;
            captured_response = response;
        },
        [](const MoonrakerError&) { FAIL("Unexpected error callback"); });

    REQUIRE(callback_called);
    REQUIRE(captured_response.contains("result"));

    const auto& result = captured_response["result"];
    REQUIRE(result.is_array());
    REQUIRE(result.size() >= 2);

    // Verify belt path CSV files are present
    bool found_path_a = false;
    bool found_path_b = false;
    for (const auto& file : result) {
        std::string path = file.value("path", "");
        if (path.find("belt_path_a") != std::string::npos)
            found_path_a = true;
        if (path.find("belt_path_b") != std::string::npos)
            found_path_b = true;
    }
    CHECK(found_path_a);
    CHECK(found_path_b);
}

// ============================================================================
// 4. Mock server.files.get_file returns CSV content
// ============================================================================

TEST_CASE("Mock get_file returns CSV content for accelerometer data", "[accel_csv][mock]") {
    PrinterState state;
    state.init_subjects(false);
    MoonrakerClientMock client(MoonrakerClientMock::PrinterType::GENERIC_COREXY);
    MoonrakerAPI api(client, state);

    bool callback_called = false;
    json captured_response;

    json params;
    params["filename"] = "data_store/raw_data_belt_path_a-20260310_120000.csv";
    params["root"] = "config";

    client.send_jsonrpc(
        "server.files.get_file", params,
        [&](const json& response) {
            callback_called = true;
            captured_response = response;
        },
        [](const MoonrakerError&) { FAIL("Unexpected error callback"); });

    REQUIRE(callback_called);
    REQUIRE(captured_response.contains("result"));
    REQUIRE(captured_response["result"].is_string());

    std::string csv_data = captured_response["result"].get<std::string>();
    // Verify CSV has expected accelerometer header
    REQUIRE(csv_data.find("#time,accel_x,accel_y,accel_z") != std::string::npos);
    // Verify CSV has data rows
    REQUIRE(csv_data.find("0.000000") != std::string::npos);
}

TEST_CASE("Mock get_file returns error for missing filename", "[accel_csv][mock]") {
    PrinterState state;
    state.init_subjects(false);
    MoonrakerClientMock client(MoonrakerClientMock::PrinterType::GENERIC_COREXY);
    MoonrakerAPI api(client, state);

    bool error_called = false;
    json params; // no filename

    client.send_jsonrpc(
        "server.files.get_file", params, [](const json&) { FAIL("Unexpected success callback"); },
        [&](const MoonrakerError& err) {
            error_called = true;
            CHECK(err.type == MoonrakerErrorType::VALIDATION_ERROR);
        });

    REQUIRE(error_called);
}

// ============================================================================
// 5. End-to-end: download_accel_csv via mock
// ============================================================================

TEST_CASE("download_accel_csv finds and downloads belt CSV via mock", "[accel_csv][e2e]") {
    PrinterState state;
    state.init_subjects(false);
    MoonrakerClientMock client(MoonrakerClientMock::PrinterType::GENERIC_COREXY);
    MoonrakerAPI api(client, state);

    MoonrakerAdvancedAPI advanced(client, api);

    std::string captured_csv;
    bool complete_called = false;
    bool error_called = false;
    std::string error_msg;

    advanced.download_accel_csv(
        "belt_path_a",
        [&](const std::string& csv_data) {
            complete_called = true;
            captured_csv = csv_data;
        },
        [&](const MoonrakerError& err) {
            error_called = true;
            error_msg = err.message;
        });

    INFO("Error message: " << error_msg);
    REQUIRE_FALSE(error_called);
    REQUIRE(complete_called);
    // CSV content from mock has accelerometer header
    REQUIRE(captured_csv.find("#time,accel_x,accel_y,accel_z") != std::string::npos);
}

TEST_CASE("download_accel_csv reports error for missing CSV name", "[accel_csv][e2e]") {
    PrinterState state;
    state.init_subjects(false);
    MoonrakerClientMock client(MoonrakerClientMock::PrinterType::GENERIC_COREXY);
    MoonrakerAPI api(client, state);

    MoonrakerAdvancedAPI advanced(client, api);

    bool complete_called = false;
    bool error_called = false;

    // Use a name that won't match any mock files
    advanced.download_accel_csv(
        "nonexistent_axis_zzz", [&](const std::string&) { complete_called = true; },
        [&](const MoonrakerError& err) {
            error_called = true;
            CHECK(err.message.find("No accelerometer data file found") != std::string::npos);
        });

    REQUIRE_FALSE(complete_called);
    REQUIRE(error_called);
}
