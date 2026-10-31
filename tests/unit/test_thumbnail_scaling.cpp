// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_thumbnail_scaling.cpp
 * @brief Unit tests for ThumbnailProcessor scaling behavior
 *
 * Verifies that pre-scaled thumbnails never exceed target dimensions.
 * The processor uses CONTAIN strategy: scaled output fits entirely within
 * the target rect, preserving aspect ratio.
 */

#include "../../include/thumbnail_processor.h"
#include "../test_helpers/thumbnail_test_png.h"

#include "../catch_amalgamated.hpp"

using helix::ThumbnailProcessor;
using helix::ThumbnailTarget;

// ============================================================================
// Test PNG data (solid-color images of various aspect ratios); the square
// 10x10 one is kTinyPng
// ============================================================================

// clang-format off
// Wide 40x20 PNG (2:1 aspect ratio, 93 bytes)
static const std::vector<uint8_t> png_40x20 = {
    0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D,
    0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x28, 0x00, 0x00, 0x00, 0x14,
    0x08, 0x02, 0x00, 0x00, 0x00, 0x70, 0x24, 0xE8, 0xEC, 0x00, 0x00, 0x00,
    0x24, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9C, 0x63, 0x68, 0x70, 0x50, 0x18,
    0x10, 0xC4, 0x30, 0x6A, 0xF1, 0xA8, 0xC5, 0xA3, 0x16, 0x8F, 0x5A, 0x3C,
    0x6A, 0xF1, 0xA8, 0xC5, 0xA3, 0x16, 0x8F, 0x5A, 0x3C, 0x72, 0x2C, 0x06,
    0x00, 0x8F, 0x66, 0xBC, 0x1F, 0xAC, 0x5F, 0xFA, 0xAA, 0x00, 0x00, 0x00,
    0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82};

// Tall 20x40 PNG (1:2 aspect ratio, 93 bytes)
static const std::vector<uint8_t> png_20x40 = {
    0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D,
    0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x14, 0x00, 0x00, 0x00, 0x28,
    0x08, 0x02, 0x00, 0x00, 0x00, 0x71, 0x53, 0x4D, 0x8C, 0x00, 0x00, 0x00,
    0x24, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9C, 0x63, 0x68, 0x70, 0x50, 0x20,
    0x1B, 0x31, 0x8C, 0x6A, 0x1E, 0xD5, 0x3C, 0xAA, 0x79, 0x54, 0xF3, 0xA8,
    0xE6, 0x51, 0xCD, 0xA3, 0x9A, 0x47, 0x35, 0x8F, 0x6A, 0x26, 0x17, 0x01,
    0x00, 0xE9, 0x0F, 0xBC, 0x1F, 0x9B, 0x10, 0x7D, 0x45, 0x00, 0x00, 0x00,
    0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82};

// Very wide 100x30 PNG (10:3 aspect ratio, 136 bytes)
static const std::vector<uint8_t> png_100x30 = {
    0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D,
    0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x64, 0x00, 0x00, 0x00, 0x1E,
    0x08, 0x02, 0x00, 0x00, 0x00, 0x55, 0x39, 0x2C, 0xA4, 0x00, 0x00, 0x00,
    0x4F, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9C, 0xED, 0xD0, 0x41, 0x09, 0x00,
    0x20, 0x00, 0xC0, 0x40, 0xA3, 0x18, 0xC5, 0x68, 0x46, 0xB7, 0x82, 0xBE,
    0x86, 0x70, 0xB0, 0x00, 0xE3, 0xC6, 0x5E, 0x53, 0x97, 0x8D, 0xFC, 0xE0,
    0xA3, 0x60, 0xC1, 0x82, 0x95, 0x07, 0x0B, 0x16, 0xAC, 0x3C, 0x58, 0xB0,
    0x60, 0xE5, 0xC1, 0x82, 0x05, 0x2B, 0x0F, 0x16, 0x2C, 0x58, 0x79, 0xB0,
    0x60, 0xC1, 0xCA, 0x83, 0x05, 0x0B, 0x56, 0x1E, 0x2C, 0x58, 0xB0, 0xF2,
    0x60, 0xC1, 0x82, 0x95, 0x07, 0xEB, 0xA1, 0x03, 0x84, 0xCF, 0x41, 0x97,
    0x89, 0x80, 0xCF, 0xC1, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44,
    0xAE, 0x42, 0x60, 0x82};
// clang-format on

// ============================================================================
// Scaling invariant: output dimensions never exceed target (CONTAIN strategy)
// ============================================================================

TEST_CASE("ThumbnailProcessor output fits within target dimensions",
          "[assets][processor][scaling]") {
    auto& processor = ThumbnailProcessor::instance();

    // Ensure cache dir exists
    std::string cache_dir = processor.get_cache_dir();
    if (cache_dir.empty()) {
        processor.set_cache_dir("/tmp/helix_thumb_test_scaling");
    }

    ThumbnailTarget target;
    target.width = 160;
    target.height = 160;
    target.color_format = 0x10; // ARGB8888

    SECTION("Square source fits exactly") {
        auto result = processor.process_sync(kTinyPng, "test_sq_10x10.png", target);
        REQUIRE(result.success);
        CHECK(result.output_width <= target.width);
        CHECK(result.output_height <= target.height);
        // Square source + square target = exact fit
        CHECK(result.output_width == target.width);
        CHECK(result.output_height == target.height);
    }

    SECTION("Wide source: width constrained, height smaller") {
        // 40x20 (2:1) → target 160x160 → should be 160x80
        auto result = processor.process_sync(png_40x20, "test_wide_40x20.png", target);
        REQUIRE(result.success);
        CHECK(result.output_width <= target.width);
        CHECK(result.output_height <= target.height);
        INFO("Output: " << result.output_width << "x" << result.output_height);
        // Width should be the constraining axis
        CHECK(result.output_width == 160);
        CHECK(result.output_height == 80);
    }

    SECTION("Tall source: height constrained, width smaller") {
        // 20x40 (1:2) → target 160x160 → should be 80x160
        auto result = processor.process_sync(png_20x40, "test_tall_20x40.png", target);
        REQUIRE(result.success);
        CHECK(result.output_width <= target.width);
        CHECK(result.output_height <= target.height);
        INFO("Output: " << result.output_width << "x" << result.output_height);
        // Height should be the constraining axis
        CHECK(result.output_width == 80);
        CHECK(result.output_height == 160);
    }

    SECTION("Very wide source: extreme aspect ratio stays within bounds") {
        // 100x30 (10:3) → target 160x160 → should be 160x48
        auto result = processor.process_sync(png_100x30, "test_vwide_100x30.png", target);
        REQUIRE(result.success);
        CHECK(result.output_width <= target.width);
        CHECK(result.output_height <= target.height);
        INFO("Output: " << result.output_width << "x" << result.output_height);
    }

    SECTION("Small target with non-square source") {
        ThumbnailTarget small_target;
        small_target.width = 120;
        small_target.height = 120;
        small_target.color_format = 0x10;

        // 40x20 → target 120x120 → should be 120x60
        auto result = processor.process_sync(png_40x20, "test_wide_small_target.png", small_target);
        REQUIRE(result.success);
        CHECK(result.output_width <= small_target.width);
        CHECK(result.output_height <= small_target.height);
    }

    SECTION("Large target with non-square source") {
        ThumbnailTarget large_target;
        large_target.width = 220;
        large_target.height = 220;
        large_target.color_format = 0x10;

        // 20x40 → target 220x220 → should be 110x220
        auto result = processor.process_sync(png_20x40, "test_tall_large_target.png", large_target);
        REQUIRE(result.success);
        CHECK(result.output_width <= large_target.width);
        CHECK(result.output_height <= large_target.height);
    }
}

TEST_CASE("ThumbnailProcessor preserves aspect ratio", "[assets][processor][scaling]") {
    auto& processor = ThumbnailProcessor::instance();

    std::string cache_dir = processor.get_cache_dir();
    if (cache_dir.empty()) {
        processor.set_cache_dir("/tmp/helix_thumb_test_scaling");
    }

    ThumbnailTarget target;
    target.width = 160;
    target.height = 160;
    target.color_format = 0x10;

    SECTION("2:1 aspect ratio preserved") {
        auto result = processor.process_sync(png_40x20, "test_ar_2to1.png", target);
        REQUIRE(result.success);
        // 2:1 ratio means width should be ~2x height
        float ratio = static_cast<float>(result.output_width) / result.output_height;
        CHECK(ratio == Catch::Approx(2.0f).margin(0.1f));
    }

    SECTION("1:2 aspect ratio preserved") {
        auto result = processor.process_sync(png_20x40, "test_ar_1to2.png", target);
        REQUIRE(result.success);
        // 1:2 ratio means height should be ~2x width
        float ratio = static_cast<float>(result.output_height) / result.output_width;
        CHECK(ratio == Catch::Approx(2.0f).margin(0.1f));
    }
}

TEST_CASE("ThumbnailProcessor handles invalid input", "[assets][processor][scaling]") {
    auto& processor = ThumbnailProcessor::instance();

    std::string cache_dir = processor.get_cache_dir();
    if (cache_dir.empty()) {
        processor.set_cache_dir("/tmp/helix_thumb_test_scaling");
    }

    ThumbnailTarget target;
    target.width = 160;
    target.height = 160;
    target.color_format = 0x10;

    SECTION("Empty PNG data fails gracefully") {
        std::vector<uint8_t> empty;
        auto result = processor.process_sync(empty, "test_empty.png", target);
        CHECK_FALSE(result.success);
        CHECK_FALSE(result.error.empty());
    }

    SECTION("Garbage data fails gracefully") {
        std::vector<uint8_t> garbage = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x01, 0x02, 0x03};
        auto result = processor.process_sync(garbage, "test_garbage.png", target);
        CHECK_FALSE(result.success);
    }
}
