// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_input_shaper_cache.cpp
 * @brief Unit tests for Input Shaper Cache functionality (Phase 7)
 *
 * Test-first development: These tests are written BEFORE implementation.
 * Tests compile and link with minimal header stub, but will FAIL until
 * the InputShaperCache implementation is complete.
 *
 * Test categories:
 * 1. JSON Serialization - ShaperOption, InputShaperResult, CalibrationResults
 * 2. Cache Save/Load - File I/O with proper error handling
 * 3. Cache Invalidation - TTL expiration, printer ID mismatch
 * 4. has_cached_results - State query functions
 */

#include "../../include/calibration_types.h"
#include "../../include/input_shaper_cache.h"
#include "../../include/input_shaper_calibrator.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "../catch_amalgamated.hpp"

namespace fs = std::filesystem;

using namespace helix::calibration;

// ============================================================================
// Test Fixture
// ============================================================================

/**
 * @brief Test fixture for InputShaperCache testing
 *
 * Provides temporary directory setup/teardown and helper methods
 * for creating test data and manipulating cache files.
 */
class InputShaperCacheTestFixture {
  public:
    InputShaperCacheTestFixture() {
        // Create unique temp directory for each test run
        temp_dir_ = fs::temp_directory_path() /
                    ("helix_cache_test_" +
                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(temp_dir_);
    }

    ~InputShaperCacheTestFixture() {
        // Clean up temp directory after test
        std::error_code ec;
        fs::remove_all(temp_dir_, ec);
        // Ignore errors - best effort cleanup
    }

    /**
     * @brief Get path to cache file in temp directory
     */
    [[nodiscard]] fs::path cache_path() const {
        return temp_dir_ / "input_shaper_cache.json";
    }

    /**
     * @brief Get path to cache directory
     */
    [[nodiscard]] fs::path cache_dir() const {
        return temp_dir_;
    }

    /**
     * @brief Create a valid ShaperOption for testing
     */
    [[nodiscard]] static ShaperOption make_shaper_option(const std::string& type = "mzv",
                                                         float freq = 36.7f, float vibr = 2.5f,
                                                         float smooth = 0.05f,
                                                         float accel = 5000.0f) {
        ShaperOption opt;
        opt.type = type;
        opt.frequency = freq;
        opt.vibrations = vibr;
        opt.smoothing = smooth;
        opt.max_accel = accel;
        return opt;
    }

    /**
     * @brief Create a valid InputShaperResult for testing
     */
    [[nodiscard]] static InputShaperResult
    make_result(char axis = 'X', const std::string& shaper_type = "mzv", float freq = 36.7f) {
        InputShaperResult result;
        result.axis = axis;
        result.shaper_type = shaper_type;
        result.shaper_freq = freq;
        result.max_accel = 5000.0f;
        result.smoothing = 0.05f;
        result.vibrations = 2.5f;

        // Add some frequency response data points
        result.freq_response = {
            {10.0f, 0.5f}, {20.0f, 1.2f}, {30.0f, 3.8f}, {40.0f, 2.1f}, {50.0f, 0.8f}};

        // Add alternative shaper options
        result.all_shapers.push_back(make_shaper_option("zv", 35.0f, 3.0f, 0.03f, 4500.0f));
        result.all_shapers.push_back(make_shaper_option("mzv", 36.7f, 2.5f, 0.05f, 5000.0f));
        result.all_shapers.push_back(make_shaper_option("ei", 38.2f, 2.0f, 0.08f, 4000.0f));

        return result;
    }

    /**
     * @brief Create complete CalibrationResults for testing
     */
    [[nodiscard]] static InputShaperCalibrator::CalibrationResults make_calibration_results() {
        InputShaperCalibrator::CalibrationResults results;
        results.x_result = make_result('X', "mzv", 36.7f);
        results.y_result = make_result('Y', "ei", 47.6f);
        results.noise_level = 22.5f;
        return results;
    }

    /**
     * @brief Write raw content to cache file
     */
    void write_cache_file(const std::string& content) {
        std::ofstream file(cache_path());
        file << content;
    }

    /**
     * @brief Read raw content from cache file
     */
    [[nodiscard]] std::string read_cache_file() const {
        std::ifstream file(cache_path());
        return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    }

    /**
     * @brief Check if cache file exists
     */
    [[nodiscard]] bool cache_file_exists() const {
        return fs::exists(cache_path());
    }

  protected:
    fs::path temp_dir_;
};

// ============================================================================
// JSON Serialization Tests - ShaperOption
// ============================================================================

TEST_CASE("ShaperOption serializes to JSON", "[cache][input_shaper][json][serialization]") {
    ShaperOption opt;
    opt.type = "mzv";
    opt.frequency = 36.7f;
    opt.vibrations = 2.5f;
    opt.smoothing = 0.05f;
    opt.max_accel = 5000.0f;

    auto json = shaper_option_to_json(opt);

    REQUIRE(json.contains("type"));
    REQUIRE(json.contains("frequency"));
    REQUIRE(json.contains("vibrations"));
    REQUIRE(json.contains("smoothing"));
    REQUIRE(json.contains("max_accel"));

    CHECK(json["type"].get<std::string>() == "mzv");
    CHECK(json["frequency"].get<float>() == Catch::Approx(36.7f));
    CHECK(json["vibrations"].get<float>() == Catch::Approx(2.5f));
    CHECK(json["smoothing"].get<float>() == Catch::Approx(0.05f));
    CHECK(json["max_accel"].get<float>() == Catch::Approx(5000.0f));
}

TEST_CASE("ShaperOption deserializes from JSON", "[cache][input_shaper][json][deserialization]") {
    nlohmann::json json = {{"type", "ei"},
                           {"frequency", 47.6f},
                           {"vibrations", 1.8f},
                           {"smoothing", 0.08f},
                           {"max_accel", 4500.0f}};

    auto opt = shaper_option_from_json(json);

    CHECK(opt.type == "ei");
    CHECK(opt.frequency == Catch::Approx(47.6f));
    CHECK(opt.vibrations == Catch::Approx(1.8f));
    CHECK(opt.smoothing == Catch::Approx(0.08f));
    CHECK(opt.max_accel == Catch::Approx(4500.0f));
}

TEST_CASE("ShaperOption roundtrip serialization", "[cache][input_shaper][json][roundtrip]") {
    ShaperOption original;
    original.type = "2hump_ei";
    original.frequency = 42.3f;
    original.vibrations = 1.2f;
    original.smoothing = 0.12f;
    original.max_accel = 3500.0f;

    auto json = shaper_option_to_json(original);
    auto restored = shaper_option_from_json(json);

    CHECK(restored.type == original.type);
    CHECK(restored.frequency == Catch::Approx(original.frequency));
    CHECK(restored.vibrations == Catch::Approx(original.vibrations));
    CHECK(restored.smoothing == Catch::Approx(original.smoothing));
    CHECK(restored.max_accel == Catch::Approx(original.max_accel));
}

// ============================================================================
// JSON Serialization Tests - InputShaperResult
// ============================================================================

TEST_CASE("InputShaperResult serializes to JSON", "[cache][input_shaper][json][serialization]") {
    InputShaperResult result;
    result.axis = 'X';
    result.shaper_type = "mzv";
    result.shaper_freq = 36.7f;
    result.max_accel = 5000.0f;
    result.smoothing = 0.05f;
    result.vibrations = 2.5f;
    result.freq_response = {{10.0f, 0.5f}, {20.0f, 1.2f}, {30.0f, 3.8f}};

    ShaperOption opt1;
    opt1.type = "zv";
    opt1.frequency = 35.0f;
    result.all_shapers.push_back(opt1);

    auto json = input_shaper_result_to_json(result);

    REQUIRE(json.contains("axis"));
    REQUIRE(json.contains("shaper_type"));
    REQUIRE(json.contains("shaper_freq"));
    REQUIRE(json.contains("max_accel"));
    REQUIRE(json.contains("smoothing"));
    REQUIRE(json.contains("vibrations"));
    REQUIRE(json.contains("freq_response"));
    REQUIRE(json.contains("all_shapers"));

    CHECK(json["axis"].get<std::string>() == "X");
    CHECK(json["shaper_type"].get<std::string>() == "mzv");
    CHECK(json["shaper_freq"].get<float>() == Catch::Approx(36.7f));
    CHECK(json["freq_response"].is_array());
    CHECK(json["freq_response"].size() == 3);
    CHECK(json["all_shapers"].is_array());
    CHECK(json["all_shapers"].size() == 1);
}

TEST_CASE("InputShaperResult deserializes from JSON",
          "[cache][input_shaper][json][deserialization]") {
    nlohmann::json json = {{"axis", "Y"},
                           {"shaper_type", "ei"},
                           {"shaper_freq", 47.6f},
                           {"max_accel", 4500.0f},
                           {"smoothing", 0.08f},
                           {"vibrations", 1.8f},
                           {"freq_response", {{15.0f, 0.8f}, {25.0f, 2.1f}}},
                           {"all_shapers",
                            {{{"type", "ei"},
                              {"frequency", 47.6f},
                              {"vibrations", 1.8f},
                              {"smoothing", 0.08f},
                              {"max_accel", 4500.0f}}}}};

    auto result = input_shaper_result_from_json(json);

    CHECK(result.axis == 'Y');
    CHECK(result.shaper_type == "ei");
    CHECK(result.shaper_freq == Catch::Approx(47.6f));
    CHECK(result.max_accel == Catch::Approx(4500.0f));
    CHECK(result.smoothing == Catch::Approx(0.08f));
    CHECK(result.vibrations == Catch::Approx(1.8f));
    CHECK(result.freq_response.size() == 2);
    CHECK(result.all_shapers.size() == 1);
}

TEST_CASE_METHOD(InputShaperCacheTestFixture, "InputShaperResult roundtrip serialization",
                 "[cache][input_shaper][json][roundtrip]") {
    auto original = make_result('X', "mzv", 36.7f);

    auto json = input_shaper_result_to_json(original);
    auto restored = input_shaper_result_from_json(json);

    CHECK(restored.axis == original.axis);
    CHECK(restored.shaper_type == original.shaper_type);
    CHECK(restored.shaper_freq == Catch::Approx(original.shaper_freq));
    CHECK(restored.max_accel == Catch::Approx(original.max_accel));
    CHECK(restored.smoothing == Catch::Approx(original.smoothing));
    CHECK(restored.vibrations == Catch::Approx(original.vibrations));
    CHECK(restored.freq_response.size() == original.freq_response.size());
    CHECK(restored.all_shapers.size() == original.all_shapers.size());
}

TEST_CASE("InputShaperResult with empty freq_response serializes",
          "[cache][input_shaper][json][edge]") {
    InputShaperResult result;
    result.axis = 'X';
    result.shaper_type = "mzv";
    result.shaper_freq = 36.7f;
    // Empty freq_response and all_shapers

    auto json = input_shaper_result_to_json(result);

    CHECK(json["freq_response"].is_array());
    CHECK(json["freq_response"].empty());
    CHECK(json["all_shapers"].is_array());
    CHECK(json["all_shapers"].empty());
}

// ============================================================================
// JSON Serialization Tests - CalibrationResults
// ============================================================================

TEST_CASE_METHOD(InputShaperCacheTestFixture, "CalibrationResults serializes complete",
                 "[cache][input_shaper][json][serialization]") {
    auto results = make_calibration_results();

    auto json = calibration_results_to_json(results);

    REQUIRE(json.contains("x_result"));
    REQUIRE(json.contains("y_result"));
    REQUIRE(json.contains("noise_level"));

    CHECK(json["noise_level"].get<float>() == Catch::Approx(22.5f));
    CHECK(json["x_result"]["axis"].get<std::string>() == "X");
    CHECK(json["y_result"]["axis"].get<std::string>() == "Y");
}

TEST_CASE_METHOD(InputShaperCacheTestFixture, "CalibrationResults roundtrip serialization",
                 "[cache][input_shaper][json][roundtrip]") {
    auto original = make_calibration_results();

    auto json = calibration_results_to_json(original);
    auto restored = calibration_results_from_json(json);

    CHECK(restored.noise_level == Catch::Approx(original.noise_level));
    CHECK(restored.x_result.axis == original.x_result.axis);
    CHECK(restored.x_result.shaper_type == original.x_result.shaper_type);
    CHECK(restored.x_result.shaper_freq == Catch::Approx(original.x_result.shaper_freq));
    CHECK(restored.y_result.axis == original.y_result.axis);
    CHECK(restored.y_result.shaper_type == original.y_result.shaper_type);
    CHECK(restored.y_result.shaper_freq == Catch::Approx(original.y_result.shaper_freq));
}

TEST_CASE("CalibrationResults with partial data serializes", "[cache][input_shaper][json][edge]") {
    InputShaperCalibrator::CalibrationResults results;
    // Only X result populated
    results.x_result.axis = 'X';
    results.x_result.shaper_type = "mzv";
    results.x_result.shaper_freq = 36.7f;
    results.noise_level = 15.0f;
    // Y result left at defaults

    auto json = calibration_results_to_json(results);

    CHECK(json.contains("x_result"));
    CHECK(json.contains("y_result"));
    CHECK(json["x_result"]["shaper_type"].get<std::string>() == "mzv");
    // Y result should have empty shaper_type
    CHECK(json["y_result"]["shaper_type"].get<std::string>().empty());
}

// ============================================================================
// Cache Save/Load Tests
// ============================================================================

TEST_CASE_METHOD(InputShaperCacheTestFixture, "save_and_load_results_roundtrip",
                 "[cache][input_shaper][io]") {
    InputShaperCache cache(cache_dir());

    auto original = make_calibration_results();
    const std::string printer_id = "test_printer_001";

    // Save results
    bool save_ok = cache.save_results(original, printer_id);
    REQUIRE(save_ok);
    REQUIRE(cache_file_exists());

    // Load results back
    auto loaded = cache.load_results(printer_id);
    REQUIRE(loaded.has_value());

    // Verify loaded data matches original
    CHECK(loaded->noise_level == Catch::Approx(original.noise_level));
    CHECK(loaded->x_result.axis == original.x_result.axis);
    CHECK(loaded->x_result.shaper_type == original.x_result.shaper_type);
    CHECK(loaded->x_result.shaper_freq == Catch::Approx(original.x_result.shaper_freq));
    CHECK(loaded->y_result.axis == original.y_result.axis);
    CHECK(loaded->y_result.shaper_type == original.y_result.shaper_type);
    CHECK(loaded->y_result.shaper_freq == Catch::Approx(original.y_result.shaper_freq));
}

TEST_CASE_METHOD(InputShaperCacheTestFixture, "load_returns_empty_when_no_cache",
                 "[cache][input_shaper][io]") {
    InputShaperCache cache(cache_dir());

    // No cache file exists yet
    REQUIRE_FALSE(cache_file_exists());

    auto loaded = cache.load_results("some_printer");
    CHECK_FALSE(loaded.has_value());
}

TEST_CASE_METHOD(InputShaperCacheTestFixture, "load_returns_empty_when_file_corrupted",
                 "[cache][input_shaper][io][edge]") {
    InputShaperCache cache(cache_dir());

    SECTION("completely invalid JSON") {
        write_cache_file("this is not valid json at all {{{");
        auto loaded = cache.load_results("test_printer");
        CHECK_FALSE(loaded.has_value());
    }

    SECTION("valid JSON but wrong structure") {
        write_cache_file(R"({"foo": "bar", "baz": 123})");
        auto loaded = cache.load_results("test_printer");
        CHECK_FALSE(loaded.has_value());
    }

    SECTION("empty file") {
        write_cache_file("");
        auto loaded = cache.load_results("test_printer");
        CHECK_FALSE(loaded.has_value());
    }

    SECTION("truncated JSON") {
        write_cache_file(R"({"version": 1, "printer_id": "test", "timestamp":)");
        auto loaded = cache.load_results("test_printer");
        CHECK_FALSE(loaded.has_value());
    }
}

TEST_CASE_METHOD(InputShaperCacheTestFixture, "save_creates_directory_if_needed",
                 "[cache][input_shaper][io]") {
    // Use a subdirectory that doesn't exist
    fs::path nested_dir = temp_dir_ / "nested" / "cache" / "dir";
    REQUIRE_FALSE(fs::exists(nested_dir));

    InputShaperCache cache(nested_dir);
    auto results = make_calibration_results();

    bool save_ok = cache.save_results(results, "test_printer");

    CHECK(save_ok);
    CHECK(fs::exists(nested_dir));
    CHECK(fs::exists(nested_dir / "input_shaper_cache.json"));
}

TEST_CASE_METHOD(InputShaperCacheTestFixture, "save_overwrites_existing_cache",
                 "[cache][input_shaper][io]") {
    InputShaperCache cache(cache_dir());
    const std::string printer_id = "test_printer";

    // Save initial results
    auto results1 = make_calibration_results();
    results1.noise_level = 10.0f;
    cache.save_results(results1, printer_id);

    // Save different results
    auto results2 = make_calibration_results();
    results2.noise_level = 25.0f;
    cache.save_results(results2, printer_id);

    // Load and verify we get the second results
    auto loaded = cache.load_results(printer_id);
    REQUIRE(loaded.has_value());
    CHECK(loaded->noise_level == Catch::Approx(25.0f));
}

// ============================================================================
// Cache Invalidation Tests
// ============================================================================

TEST_CASE_METHOD(InputShaperCacheTestFixture, "cache_invalidated_when_printer_id_differs",
                 "[cache][input_shaper][invalidation]") {
    InputShaperCache cache(cache_dir());

    // Save with one printer ID
    auto results = make_calibration_results();
    cache.save_results(results, "printer_A");

    // Try to load with different printer ID
    auto loaded = cache.load_results("printer_B");

    // Should return empty - cache not valid for this printer
    CHECK_FALSE(loaded.has_value());
}

TEST_CASE_METHOD(InputShaperCacheTestFixture, "cache_invalidated_when_ttl_expired",
                 "[cache][input_shaper][invalidation]") {
    InputShaperCache cache(cache_dir());
    const std::string printer_id = "test_printer";

    // Save results
    auto results = make_calibration_results();
    cache.save_results(results, printer_id);

    // Manually modify the timestamp in the cache file to be old
    // (31 days ago - exceeds 30-day TTL)
    auto content = read_cache_file();
    auto json = nlohmann::json::parse(content);

    // Set timestamp to 31 days ago
    auto old_time = std::chrono::system_clock::now() - std::chrono::hours(31 * 24);
    auto old_timestamp =
        std::chrono::duration_cast<std::chrono::seconds>(old_time.time_since_epoch()).count();
    json["timestamp"] = old_timestamp;

    write_cache_file(json.dump());

    // Try to load - should return empty due to TTL expiration
    auto loaded = cache.load_results(printer_id);
    CHECK_FALSE(loaded.has_value());
}

TEST_CASE_METHOD(InputShaperCacheTestFixture, "cache_valid_within_ttl",
                 "[cache][input_shaper][invalidation]") {
    InputShaperCache cache(cache_dir());
    const std::string printer_id = "test_printer";

    // Save results
    auto results = make_calibration_results();
    cache.save_results(results, printer_id);

    // Manually modify the timestamp to be 29 days ago (within TTL)
    auto content = read_cache_file();
    auto json = nlohmann::json::parse(content);

    auto recent_time = std::chrono::system_clock::now() - std::chrono::hours(29 * 24);
    auto recent_timestamp =
        std::chrono::duration_cast<std::chrono::seconds>(recent_time.time_since_epoch()).count();
    json["timestamp"] = recent_timestamp;

    write_cache_file(json.dump());

    // Should still load successfully
    auto loaded = cache.load_results(printer_id);
    CHECK(loaded.has_value());
}

TEST_CASE_METHOD(InputShaperCacheTestFixture, "clear_cache_removes_file",
                 "[cache][input_shaper][invalidation]") {
    InputShaperCache cache(cache_dir());

    // Save some results
    auto results = make_calibration_results();
    cache.save_results(results, "test_printer");
    REQUIRE(cache_file_exists());

    // Clear the cache
    cache.clear_cache();

    // File should be gone
    CHECK_FALSE(cache_file_exists());
}

TEST_CASE_METHOD(InputShaperCacheTestFixture, "clear_cache_safe_when_no_file",
                 "[cache][input_shaper][invalidation][edge]") {
    InputShaperCache cache(cache_dir());

    // No file exists
    REQUIRE_FALSE(cache_file_exists());

    // Clear should not throw
    REQUIRE_NOTHROW(cache.clear_cache());
}

// ============================================================================
// has_cached_results Tests
// ============================================================================

TEST_CASE_METHOD(InputShaperCacheTestFixture, "has_cached_results_false_when_no_cache",
                 "[cache][input_shaper][query]") {
    InputShaperCache cache(cache_dir());

    CHECK_FALSE(cache.has_cached_results("test_printer"));
}

TEST_CASE_METHOD(InputShaperCacheTestFixture, "has_cached_results_true_after_save",
                 "[cache][input_shaper][query]") {
    InputShaperCache cache(cache_dir());
    const std::string printer_id = "test_printer";

    auto results = make_calibration_results();
    cache.save_results(results, printer_id);

    CHECK(cache.has_cached_results(printer_id));
}

TEST_CASE_METHOD(InputShaperCacheTestFixture, "has_cached_results_false_after_clear",
                 "[cache][input_shaper][query]") {
    InputShaperCache cache(cache_dir());
    const std::string printer_id = "test_printer";

    auto results = make_calibration_results();
    cache.save_results(results, printer_id);
    REQUIRE(cache.has_cached_results(printer_id));

    cache.clear_cache();

    CHECK_FALSE(cache.has_cached_results(printer_id));
}

TEST_CASE_METHOD(InputShaperCacheTestFixture, "has_cached_results_false_for_different_printer",
                 "[cache][input_shaper][query]") {
    InputShaperCache cache(cache_dir());

    auto results = make_calibration_results();
    cache.save_results(results, "printer_A");

    CHECK(cache.has_cached_results("printer_A"));
    CHECK_FALSE(cache.has_cached_results("printer_B"));
}

TEST_CASE_METHOD(InputShaperCacheTestFixture, "has_cached_results_false_when_expired",
                 "[cache][input_shaper][query]") {
    InputShaperCache cache(cache_dir());
    const std::string printer_id = "test_printer";

    auto results = make_calibration_results();
    cache.save_results(results, printer_id);

    // Manually expire the cache
    auto content = read_cache_file();
    auto json = nlohmann::json::parse(content);
    auto old_time = std::chrono::system_clock::now() - std::chrono::hours(31 * 24);
    json["timestamp"] =
        std::chrono::duration_cast<std::chrono::seconds>(old_time.time_since_epoch()).count();
    write_cache_file(json.dump());

    CHECK_FALSE(cache.has_cached_results(printer_id));
}

// ============================================================================
// Cache File Format Tests
// ============================================================================

TEST_CASE_METHOD(InputShaperCacheTestFixture, "cache_file_contains_version",
                 "[cache][input_shaper][format]") {
    InputShaperCache cache(cache_dir());

    auto results = make_calibration_results();
    cache.save_results(results, "test_printer");

    auto content = read_cache_file();
    auto json = nlohmann::json::parse(content);

    REQUIRE(json.contains("version"));
    CHECK(json["version"].get<int>() == 1);
}

TEST_CASE_METHOD(InputShaperCacheTestFixture, "cache_file_contains_printer_id",
                 "[cache][input_shaper][format]") {
    InputShaperCache cache(cache_dir());

    auto results = make_calibration_results();
    cache.save_results(results, "my_printer_123");

    auto content = read_cache_file();
    auto json = nlohmann::json::parse(content);

    REQUIRE(json.contains("printer_id"));
    CHECK(json["printer_id"].get<std::string>() == "my_printer_123");
}

TEST_CASE_METHOD(InputShaperCacheTestFixture, "cache_file_contains_timestamp",
                 "[cache][input_shaper][format]") {
    InputShaperCache cache(cache_dir());

    auto before = std::chrono::system_clock::now();
    auto results = make_calibration_results();
    cache.save_results(results, "test_printer");
    auto after = std::chrono::system_clock::now();

    auto content = read_cache_file();
    auto json = nlohmann::json::parse(content);

    REQUIRE(json.contains("timestamp"));

    auto timestamp = json["timestamp"].get<int64_t>();
    auto before_ts =
        std::chrono::duration_cast<std::chrono::seconds>(before.time_since_epoch()).count();
    auto after_ts =
        std::chrono::duration_cast<std::chrono::seconds>(after.time_since_epoch()).count();

    CHECK(timestamp >= before_ts);
    CHECK(timestamp <= after_ts);
}

TEST_CASE_METHOD(InputShaperCacheTestFixture, "cache_rejects_unknown_version",
                 "[cache][input_shaper][format][edge]") {
    InputShaperCache cache(cache_dir());

    // Write cache with future version
    nlohmann::json json = {{"version", 99}, // Unknown future version
                           {"printer_id", "test_printer"},
                           {"timestamp", std::chrono::duration_cast<std::chrono::seconds>(
                                             std::chrono::system_clock::now().time_since_epoch())
                                             .count()},
                           {"noise_level", 22.5f},
                           {"x_result", {}},
                           {"y_result", {}}};
    write_cache_file(json.dump());

    auto loaded = cache.load_results("test_printer");
    CHECK_FALSE(loaded.has_value());
}

// ============================================================================
// Edge Cases and Error Handling
// ============================================================================

TEST_CASE_METHOD(InputShaperCacheTestFixture, "cache_handles_empty_printer_id",
                 "[cache][input_shaper][edge]") {
    InputShaperCache cache(cache_dir());

    auto results = make_calibration_results();

    // Empty printer ID should still work (or be handled gracefully)
    REQUIRE_NOTHROW(cache.save_results(results, ""));
}

TEST_CASE_METHOD(InputShaperCacheTestFixture, "cache_handles_special_characters_in_printer_id",
                 "[cache][input_shaper][edge]") {
    InputShaperCache cache(cache_dir());

    auto results = make_calibration_results();
    const std::string special_id = "printer/with:special\"chars\n";

    bool save_ok = cache.save_results(results, special_id);
    REQUIRE(save_ok);

    auto loaded = cache.load_results(special_id);
    CHECK(loaded.has_value());
}

TEST_CASE_METHOD(InputShaperCacheTestFixture, "cache_handles_very_long_printer_id",
                 "[cache][input_shaper][edge]") {
    InputShaperCache cache(cache_dir());

    auto results = make_calibration_results();
    std::string long_id(1000, 'x'); // 1000 character printer ID

    bool save_ok = cache.save_results(results, long_id);
    REQUIRE(save_ok);

    auto loaded = cache.load_results(long_id);
    CHECK(loaded.has_value());
}

TEST_CASE_METHOD(InputShaperCacheTestFixture, "cache_preserves_freq_response_data",
                 "[cache][input_shaper][data_integrity]") {
    InputShaperCache cache(cache_dir());
    const std::string printer_id = "test_printer";

    auto results = make_calibration_results();
    // Add more frequency response data
    results.x_result.freq_response = {{5.0f, 0.1f},  {10.0f, 0.5f}, {15.0f, 1.2f}, {20.0f, 2.8f},
                                      {25.0f, 4.5f}, {30.0f, 6.2f}, {35.0f, 5.8f}, {40.0f, 3.9f},
                                      {45.0f, 2.1f}, {50.0f, 1.0f}, {55.0f, 0.4f}, {60.0f, 0.2f}};

    cache.save_results(results, printer_id);
    auto loaded = cache.load_results(printer_id);

    REQUIRE(loaded.has_value());
    REQUIRE(loaded->x_result.freq_response.size() == results.x_result.freq_response.size());

    for (size_t i = 0; i < results.x_result.freq_response.size(); ++i) {
        CHECK(loaded->x_result.freq_response[i].first ==
              Catch::Approx(results.x_result.freq_response[i].first));
        CHECK(loaded->x_result.freq_response[i].second ==
              Catch::Approx(results.x_result.freq_response[i].second));
    }
}

TEST_CASE_METHOD(InputShaperCacheTestFixture, "cache_preserves_all_shapers",
                 "[cache][input_shaper][data_integrity]") {
    InputShaperCache cache(cache_dir());
    const std::string printer_id = "test_printer";

    auto results = make_calibration_results();
    cache.save_results(results, printer_id);
    auto loaded = cache.load_results(printer_id);

    REQUIRE(loaded.has_value());
    REQUIRE(loaded->x_result.all_shapers.size() == results.x_result.all_shapers.size());

    for (size_t i = 0; i < results.x_result.all_shapers.size(); ++i) {
        CHECK(loaded->x_result.all_shapers[i].type == results.x_result.all_shapers[i].type);
        CHECK(loaded->x_result.all_shapers[i].frequency ==
              Catch::Approx(results.x_result.all_shapers[i].frequency));
    }
}

// ============================================================================
// Default Cache Path Tests
// ============================================================================

TEST_CASE("InputShaperCache default constructor uses standard path",
          "[cache][input_shaper][path]") {
    // Default constructor should use a sensible default path
    InputShaperCache cache;

    // Cache should be constructible without specifying path
    // Implementation should use ~/.cache/helixscreen or similar
    REQUIRE_NOTHROW(cache.has_cached_results("test"));
}

TEST_CASE("InputShaperCache get_cache_path returns configured path",
          "[cache][input_shaper][path]") {
    fs::path custom_path = "/custom/cache/path";
    InputShaperCache cache(custom_path);

    CHECK(cache.get_cache_path() == custom_path / "input_shaper_cache.json");
}

// GAP 7: the default constructor runs determine_cache_dir(), the ONE migrated
// candidate-list site linked into the test binary. It walks
// helix::paths::xdg_cache_bases() and selects the first base where
// (ensure_dir && probe_writable) succeeds. Setting XDG_CACHE_HOME to a fresh,
// writable dir must make the resolved cache path land under <xdg>/helix/ —
// proving XDG-first ordering + the probe actually SELECT correctly, not just
// "don't throw". A regression that dropped XDG-first, mis-joined "helix", or let
// probe_writable pass on the wrong base would move the path out of this subtree.
TEST_CASE("InputShaperCache default ctor selects writable XDG cache base",
          "[cache][input_shaper][path]") {
    // Local RAII save/restore so we don't leak env state into other tests.
    struct EnvGuard {
        const char* name;
        bool had;
        std::string saved;
        explicit EnvGuard(const char* n) : name(n) {
            const char* v = std::getenv(n);
            had = v != nullptr;
            if (had) {
                saved = v;
            }
        }
        ~EnvGuard() {
            if (had) {
                ::setenv(name, saved.c_str(), 1);
            } else {
                ::unsetenv(name);
            }
        }
    };

    EnvGuard xdg_guard("XDG_CACHE_HOME");

    fs::path xdg = fs::temp_directory_path() /
                   ("helix_is_xdg_" +
                    std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::remove_all(xdg);
    fs::create_directories(xdg);
    ::setenv("XDG_CACHE_HOME", xdg.string().c_str(), 1);

    InputShaperCache cache; // default ctor -> determine_cache_dir()
    fs::path expected_dir = xdg / "helix";

    CHECK(cache.get_cache_path() == expected_dir / "input_shaper_cache.json");

    fs::remove_all(xdg);
}
