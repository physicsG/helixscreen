// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_thumbnail_selection.cpp
 * @brief Unit tests for ThumbnailInfo and the unmeasured-box (largest) thumbnail choice
 *
 * Tests the thumbnail selection logic that picks the largest available
 * thumbnail by pixel count for best display quality.
 */

#include "../../include/moonraker_api.h"

#include "../catch_amalgamated.hpp"

// ============================================================================
// ThumbnailInfo Tests
// ============================================================================

TEST_CASE("ThumbnailInfo pixel_count calculation", "[assets]") {
    SECTION("Calculates correct pixel count for standard dimensions") {
        ThumbnailInfo info;
        info.width = 300;
        info.height = 300;
        REQUIRE(info.pixel_count() == 90000);
    }

    SECTION("Calculates correct pixel count for rectangular thumbnails") {
        ThumbnailInfo info;
        info.width = 400;
        info.height = 300;
        REQUIRE(info.pixel_count() == 120000);
    }

    SECTION("Returns zero for uninitialized thumbnail") {
        ThumbnailInfo info;
        REQUIRE(info.pixel_count() == 0);
    }

    SECTION("Handles small thumbnails") {
        ThumbnailInfo info;
        info.width = 32;
        info.height = 32;
        REQUIRE(info.pixel_count() == 1024);
    }
}

// ============================================================================
// Largest thumbnail for an unmeasured box
// ============================================================================

static std::string largest(const FileMetadata& metadata) {
    return helix::select_and_resolve_thumbnail(metadata.thumbnails, "", 0, 0);
}

TEST_CASE("FileMetadata thumbnails: an unmeasured box takes the largest", "[assets]") {
    SECTION("Returns empty string when no thumbnails") {
        FileMetadata metadata;
        REQUIRE(largest(metadata).empty());
    }

    SECTION("Returns only thumbnail when one available") {
        FileMetadata metadata;
        ThumbnailInfo thumb;
        thumb.relative_path = ".thumbnails/test-300x300.png";
        thumb.width = 300;
        thumb.height = 300;
        metadata.thumbnails.push_back(thumb);

        REQUIRE(largest(metadata) == ".thumbnails/test-300x300.png");
    }

    SECTION("Selects largest thumbnail by pixel count") {
        FileMetadata metadata;

        // Small thumbnail (32x32 = 1024 pixels)
        ThumbnailInfo small;
        small.relative_path = ".thumbnails/test-32x32.png";
        small.width = 32;
        small.height = 32;
        metadata.thumbnails.push_back(small);

        // Medium thumbnail (150x150 = 22500 pixels)
        ThumbnailInfo medium;
        medium.relative_path = ".thumbnails/test-150x150.png";
        medium.width = 150;
        medium.height = 150;
        metadata.thumbnails.push_back(medium);

        // Large thumbnail (300x300 = 90000 pixels)
        ThumbnailInfo large;
        large.relative_path = ".thumbnails/test-300x300.png";
        large.width = 300;
        large.height = 300;
        metadata.thumbnails.push_back(large);

        REQUIRE(largest(metadata) == ".thumbnails/test-300x300.png");
    }

    SECTION("Handles thumbnails in any order") {
        FileMetadata metadata;

        // Add largest first
        ThumbnailInfo large;
        large.relative_path = ".thumbnails/test-300x300.png";
        large.width = 300;
        large.height = 300;
        metadata.thumbnails.push_back(large);

        // Add smallest last
        ThumbnailInfo small;
        small.relative_path = ".thumbnails/test-32x32.png";
        small.width = 32;
        small.height = 32;
        metadata.thumbnails.push_back(small);

        REQUIRE(largest(metadata) == ".thumbnails/test-300x300.png");
    }

    SECTION("Handles rectangular thumbnails correctly") {
        FileMetadata metadata;

        // 400x300 = 120000 pixels
        ThumbnailInfo rect;
        rect.relative_path = ".thumbnails/test-400x300.png";
        rect.width = 400;
        rect.height = 300;
        metadata.thumbnails.push_back(rect);

        // 300x300 = 90000 pixels (smaller even though same height)
        ThumbnailInfo square;
        square.relative_path = ".thumbnails/test-300x300.png";
        square.width = 300;
        square.height = 300;
        metadata.thumbnails.push_back(square);

        REQUIRE(largest(metadata) == ".thumbnails/test-400x300.png");
    }

    SECTION("Falls back to first thumbnail when dimensions are zero") {
        FileMetadata metadata;

        // First thumbnail with no dimensions
        ThumbnailInfo first;
        first.relative_path = ".thumbnails/test-first.png";
        first.width = 0;
        first.height = 0;
        metadata.thumbnails.push_back(first);

        // Second thumbnail with no dimensions
        ThumbnailInfo second;
        second.relative_path = ".thumbnails/test-second.png";
        second.width = 0;
        second.height = 0;
        metadata.thumbnails.push_back(second);

        // When all have 0 pixels, returns first (stable selection)
        REQUIRE(largest(metadata) == ".thumbnails/test-first.png");
    }

    SECTION("Prefers thumbnail with dimensions over ones without") {
        FileMetadata metadata;

        // Thumbnail without dimensions
        ThumbnailInfo no_dims;
        no_dims.relative_path = ".thumbnails/test-unknown.png";
        no_dims.width = 0;
        no_dims.height = 0;
        metadata.thumbnails.push_back(no_dims);

        // Thumbnail with dimensions
        ThumbnailInfo with_dims;
        with_dims.relative_path = ".thumbnails/test-300x300.png";
        with_dims.width = 300;
        with_dims.height = 300;
        metadata.thumbnails.push_back(with_dims);

        REQUIRE(largest(metadata) == ".thumbnails/test-300x300.png");
    }
}

// ============================================================================
// ThumbnailProcessor Resolution Target Tests
// ============================================================================

#include "../../include/thumbnail_processor.h"

using helix::ThumbnailProcessor;
using helix::ThumbnailTarget;

TEST_CASE("ThumbnailProcessor breakpoint selection", "[assets][processor]") {
    SECTION("SMALL breakpoint: 480x320 → 120x120") {
        auto target = ThumbnailProcessor::get_target_for_resolution(480, 320);
        REQUIRE(target.width == 120);
        REQUIRE(target.height == 120);
    }

    SECTION("SMALL breakpoint: 320x480 (portrait) → 120x120") {
        auto target = ThumbnailProcessor::get_target_for_resolution(320, 480);
        REQUIRE(target.width == 120);
        REQUIRE(target.height == 120);
    }

    SECTION("MEDIUM breakpoint: 800x480 (AD5M) → 160x160") {
        auto target = ThumbnailProcessor::get_target_for_resolution(800, 480);
        REQUIRE(target.width == 160);
        REQUIRE(target.height == 160);
    }

    SECTION("MEDIUM breakpoint: 640x480 → 160x160") {
        auto target = ThumbnailProcessor::get_target_for_resolution(640, 480);
        REQUIRE(target.width == 160);
        REQUIRE(target.height == 160);
    }

    SECTION("LARGE breakpoint: 1024x600 → 220x220") {
        auto target = ThumbnailProcessor::get_target_for_resolution(1024, 600);
        REQUIRE(target.width == 220);
        REQUIRE(target.height == 220);
    }

    SECTION("LARGE breakpoint: 1280x720 → 220x220") {
        auto target = ThumbnailProcessor::get_target_for_resolution(1280, 720);
        REQUIRE(target.width == 220);
        REQUIRE(target.height == 220);
    }

    SECTION("Boundary: exactly 480px → SMALL") {
        auto target = ThumbnailProcessor::get_target_for_resolution(480, 320);
        REQUIRE(target.width == 120);
    }

    SECTION("Boundary: 481px → MEDIUM") {
        auto target = ThumbnailProcessor::get_target_for_resolution(481, 320);
        REQUIRE(target.width == 160);
    }

    SECTION("Boundary: exactly 800px → MEDIUM") {
        auto target = ThumbnailProcessor::get_target_for_resolution(800, 600);
        REQUIRE(target.width == 160);
    }

    SECTION("Boundary: 801px → LARGE") {
        auto target = ThumbnailProcessor::get_target_for_resolution(801, 600);
        REQUIRE(target.width == 220);
    }
}

// ============================================================================
// Card-fitted thumbnail targets (#1208)
//
// The resolution ladder above infers a card size from the display. Those guesses
// drifted from what PrintSelectPanel actually lays out, so the pre-scaled .bin
// overhung the card and LVGL cropped the model. get_target_for_card() derives the
// target from the measured card instead.
// ============================================================================

namespace {

// print_file_card.xml centres the art, then style_translate_y="#preview_offset_y"
// (-12% of the image's own height) lifts it clear of the metadata overlay. Returns
// the gap between the card's top edge and the art's top edge; negative means the
// top of the model is cropped, which is the defect #1208 reported.
double art_top_gap(int card_height, int side) {
    return (card_height - side) / 2.0 - 0.12 * side;
}

} // namespace

TEST_CASE("Card thumbnail target fits the measured card", "[assets][processor][1208]") {
    SECTION("480x272 micro card (138x115) is height-bound and no longer crops") {
        auto target = ThumbnailProcessor::get_target_for_card(138, 115);

        // The old ladder handed this card a 120x120 .bin, which hung 17px off the top.
        REQUIRE(art_top_gap(115, 120) < 0.0);

        REQUIRE(target.width == target.height);
        REQUIRE(target.height <= 115);
        REQUIRE(target.width <= 138);
        REQUIRE(art_top_gap(115, target.height) >= 0.0);
    }

    SECTION("800x480 card (132x205) is width-bound") {
        auto target = ThumbnailProcessor::get_target_for_card(132, 205);

        // The old ladder handed this card a 160x160 .bin — 28px wider than the card.
        REQUIRE(target.width <= 132);
        REQUIRE(art_top_gap(205, target.height) >= 0.0);
    }

    SECTION("Art stays inside the card across the whole layout range") {
        for (int w = 130; w <= 230; w += 2) {
            for (int h = 90; h <= 260; h += 2) {
                auto target = ThumbnailProcessor::get_target_for_card(w, h);
                INFO("card " << w << "x" << h << " -> " << target.width << "x" << target.height);
                REQUIRE(target.width <= w);
                REQUIRE(art_top_gap(h, target.height) >= 0.0);
            }
        }
    }

    SECTION("Sides snap to a multiple of 4 so the .bin cache stays small") {
        for (int h = 100; h <= 200; h += 1) {
            auto target = ThumbnailProcessor::get_target_for_card(230, h);
            REQUIRE(target.width % 4 == 0);
        }
    }

    SECTION("Oversized cards clamp to the 220px ceiling") {
        auto target = ThumbnailProcessor::get_target_for_card(400, 600);
        REQUIRE(target.width == 220);
        REQUIRE(target.height == 220);
    }

    SECTION("Degenerate cards clamp to a usable floor rather than vanishing") {
        auto target = ThumbnailProcessor::get_target_for_card(20, 20);
        REQUIRE(target.width == 64);
        REQUIRE(target.height == 64);
    }

    SECTION("Invalid dimensions fall back to the smallest ladder size") {
        auto zero = ThumbnailProcessor::get_target_for_card(0, 0);
        REQUIRE(zero.width == 120);
        REQUIRE(zero.height == 120);

        auto negative = ThumbnailProcessor::get_target_for_card(-138, -115);
        REQUIRE(negative.width == 120);
        REQUIRE(negative.height == 120);
    }

    SECTION("Always ARGB8888, like every other target") {
        auto target = ThumbnailProcessor::get_target_for_card(138, 115);
        REQUIRE(target.color_format == 0x10);
    }
}

TEST_CASE("Card size hint steers Card-size lookups", "[assets][processor][1208]") {
    // Restore the ladder for every other test in the binary.
    struct HintReset {
        ~HintReset() {
            ThumbnailProcessor::set_card_size_hint(0, 0);
        }
    } reset;

    SECTION("A measured card overrides the resolution ladder") {
        ThumbnailProcessor::set_card_size_hint(138, 115);
        auto target = ThumbnailProcessor::get_target_for_display(helix::ThumbnailSize::Card);
        REQUIRE(target == ThumbnailProcessor::get_target_for_card(138, 115));
        REQUIRE(target.height < 120); // strictly smaller than the ladder's guess
    }

    SECTION("Detail lookups ignore the card hint") {
        ThumbnailProcessor::set_card_size_hint(138, 115);
        auto detail = ThumbnailProcessor::get_target_for_display(helix::ThumbnailSize::Detail);
        REQUIRE(detail.width >= 200);
    }

    SECTION("Clearing the hint restores the ladder") {
        ThumbnailProcessor::set_card_size_hint(138, 115);
        auto hinted = ThumbnailProcessor::get_target_for_display(helix::ThumbnailSize::Card);

        ThumbnailProcessor::set_card_size_hint(0, 0);
        auto unhinted = ThumbnailProcessor::get_target_for_display(helix::ThumbnailSize::Card);

        REQUIRE_FALSE(hinted == unhinted);
        REQUIRE(unhinted.width >= 120);
    }

    SECTION("A garbage hint is rejected, not stored") {
        ThumbnailProcessor::set_card_size_hint(0, 0);
        auto ladder = ThumbnailProcessor::get_target_for_display(helix::ThumbnailSize::Card);

        ThumbnailProcessor::set_card_size_hint(-5, 115);
        REQUIRE(ThumbnailProcessor::get_target_for_display(helix::ThumbnailSize::Card) == ladder);
    }
}

TEST_CASE("ThumbnailProcessor color format is always ARGB8888", "[assets][processor]") {
    SECTION("Card size is ARGB8888 (0x10)") {
        auto target = ThumbnailProcessor::get_target_for_resolution(800, 480);
        REQUIRE(target.color_format == 0x10);
    }

    SECTION("Detail size is also ARGB8888 (0x10)") {
        using helix::ThumbnailSize;
        auto target =
            ThumbnailProcessor::get_target_for_resolution(800, 480, ThumbnailSize::Detail);
        REQUIRE(target.color_format == 0x10);
    }
}

TEST_CASE("ThumbnailProcessor uses max(width, height) for breakpoint", "[assets][processor]") {
    SECTION("Portrait 600x1024 uses 1024 → LARGE") {
        auto target = ThumbnailProcessor::get_target_for_resolution(600, 1024);
        REQUIRE(target.width == 220);
    }

    SECTION("Landscape 1024x600 uses 1024 → LARGE") {
        auto target = ThumbnailProcessor::get_target_for_resolution(1024, 600);
        REQUIRE(target.width == 220);
    }

    SECTION("Square 800x800 uses 800 → MEDIUM") {
        auto target = ThumbnailProcessor::get_target_for_resolution(800, 800);
        REQUIRE(target.width == 160);
    }
}

TEST_CASE("ThumbnailProcessor edge cases", "[assets][processor]") {
    SECTION("Zero dimensions → SMALL fallback") {
        auto target = ThumbnailProcessor::get_target_for_resolution(0, 0);
        REQUIRE(target.width == 120);
        REQUIRE(target.height == 120);
    }

    SECTION("Negative width → SMALL fallback") {
        auto target = ThumbnailProcessor::get_target_for_resolution(-100, 480);
        REQUIRE(target.width == 120);
    }

    SECTION("Negative height → SMALL fallback") {
        auto target = ThumbnailProcessor::get_target_for_resolution(800, -1);
        REQUIRE(target.width == 120);
    }

    SECTION("Very large display (4K) → LARGE") {
        auto target = ThumbnailProcessor::get_target_for_resolution(3840, 2160);
        REQUIRE(target.width == 220);
    }

    SECTION("Zero dimensions always ARGB8888") {
        auto target = ThumbnailProcessor::get_target_for_resolution(0, 0);
        REQUIRE(target.color_format == 0x10);
    }
}

TEST_CASE("ThumbnailProcessor detail size breakpoints", "[assets][processor]") {
    using helix::ThumbnailSize;

    SECTION("SMALL detail: 480x320 → 200x200") {
        auto target =
            ThumbnailProcessor::get_target_for_resolution(480, 320, ThumbnailSize::Detail);
        REQUIRE(target.width == 200);
        REQUIRE(target.height == 200);
    }

    SECTION("MEDIUM detail: 800x480 → 300x300") {
        auto target =
            ThumbnailProcessor::get_target_for_resolution(800, 480, ThumbnailSize::Detail);
        REQUIRE(target.width == 300);
        REQUIRE(target.height == 300);
    }

    SECTION("LARGE detail: 1024x600 → 400x400") {
        auto target =
            ThumbnailProcessor::get_target_for_resolution(1024, 600, ThumbnailSize::Detail);
        REQUIRE(target.width == 400);
        REQUIRE(target.height == 400);
    }

    SECTION("Detail zero dimensions → 200x200 fallback") {
        auto target = ThumbnailProcessor::get_target_for_resolution(0, 0, ThumbnailSize::Detail);
        REQUIRE(target.width == 200);
        REQUIRE(target.height == 200);
    }

    SECTION("Detail always ARGB8888") {
        auto target =
            ThumbnailProcessor::get_target_for_resolution(800, 480, ThumbnailSize::Detail);
        REQUIRE(target.color_format == 0x10);
    }
}
