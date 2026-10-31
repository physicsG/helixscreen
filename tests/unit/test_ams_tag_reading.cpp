// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

// The backend-neutral tag reading: provenance, Reset to tag, and the fold that
// keeps a tag's values while our own write-back holds the box record.

#include "ams_tag_reading.h"
#include "ams_types.h"

#include "catch_amalgamated.hpp"

using helix::SlotInfo;
using helix::ams::fold_tag_frame;
using helix::ams::reset_to_tag;
using helix::ams::tag_provenance;
using helix::ams::TagField;
using helix::ams::TagReading;

namespace {

TagReading petg_tag() {
    TagReading tag;
    tag.material = "PETG";
    tag.brand = "Snapmaker";
    tag.product_name = "Matte";
    tag.color_rgb = 0xED2C2Cu;
    tag.nozzle_temp_min = 230;
    tag.nozzle_temp_max = 250;
    tag.bed_temp = 80;
    tag.total_weight_g = 1000.0F;
    return tag;
}

SlotInfo shown_as(const TagReading& tag) {
    SlotInfo slot;
    slot.material = *tag.material;
    slot.brand = *tag.brand;
    slot.product_name = *tag.product_name;
    slot.color_rgb = *tag.color_rgb;
    slot.nozzle_temp_min = *tag.nozzle_temp_min;
    slot.nozzle_temp_max = *tag.nozzle_temp_max;
    slot.bed_temp = *tag.bed_temp;
    slot.total_weight_g = *tag.total_weight_g;
    return slot;
}

constexpr TagField kAll[] = {TagField::Material, TagField::Brand,      TagField::Product,
                             TagField::Color,    TagField::NozzleTemp, TagField::BedTemp,
                             TagField::Weight};

} // namespace

TEST_CASE("tag provenance marks every field the slot shows as the tag stated it",
          "[ams][tag_reading]") {
    const TagReading tag = petg_tag();
    SlotInfo slot = shown_as(tag);
    // Text compares case-insensitively: firmware and the catalog spell alike
    // words differently.
    slot.material = "petg";

    const auto p = tag_provenance(tag, slot);
    for (const TagField f : kAll) {
        CHECK(p.from_tag(f));
    }
}

TEST_CASE("tag provenance clears exactly the field a user moved", "[ams][tag_reading]") {
    const TagReading tag = petg_tag();

    SECTION("colour") {
        SlotInfo slot = shown_as(tag);
        slot.color_rgb = 0x00FF00u;
        const auto p = tag_provenance(tag, slot);
        CHECK_FALSE(p.from_tag(TagField::Color));
        CHECK(p.from_tag(TagField::Material));
        CHECK(p.from_tag(TagField::Brand));
    }
    SECTION("a multi-colour the tag did not state") {
        SlotInfo slot = shown_as(tag);
        slot.multi_color_hexes = "#ED2C2C,#00FF00";
        CHECK_FALSE(tag_provenance(tag, slot).from_tag(TagField::Color));
    }
    SECTION("one half of the nozzle pair") {
        SlotInfo slot = shown_as(tag);
        slot.nozzle_temp_max = 260;
        CHECK_FALSE(tag_provenance(tag, slot).from_tag(TagField::NozzleTemp));
    }
    SECTION("weight") {
        SlotInfo slot = shown_as(tag);
        slot.total_weight_g = 750.0F;
        CHECK_FALSE(tag_provenance(tag, slot).from_tag(TagField::Weight));
    }
}

TEST_CASE("tag provenance never claims a field the tag left out", "[ams][tag_reading]") {
    TagReading tag;
    tag.color_rgb = 0xFF8800u;
    SlotInfo slot;
    slot.color_rgb = 0xFF8800u;
    slot.material = "PLA";

    const auto p = tag_provenance(tag, slot);
    CHECK(p.from_tag(TagField::Color));
    CHECK_FALSE(p.from_tag(TagField::Material));
    CHECK_FALSE(p.from_tag(TagField::NozzleTemp));
    CHECK_FALSE(p.from_tag(TagField::Weight));

    TagReading half;
    half.nozzle_temp_min = 200;
    SlotInfo temps;
    temps.nozzle_temp_min = 200;
    temps.nozzle_temp_max = 999;
    CHECK(tag_provenance(half, temps).from_tag(TagField::NozzleTemp));
    CHECK_FALSE(tag_provenance(TagReading{}, temps).any());
}

TEST_CASE("Reset to tag restores the tag's fields and leaves the rest", "[ams][tag_reading]") {
    TagReading tag;
    tag.material = "PETG";
    tag.color_rgb = 0xED2C2Cu;
    tag.multi_color_hexes = "#ED2C2C,#00FF00";

    SlotInfo edited;
    edited.material = "PLA";
    edited.brand = "eSUN";
    edited.color_rgb = 0x123456u;
    edited.color_name = "Navy";
    edited.catalog_id = "esun-pla-plus";
    edited.remaining_weight_g = 400.0F;

    const SlotInfo reset = reset_to_tag(edited, tag);
    CHECK(reset.material == "PETG");
    CHECK(reset.color_rgb == 0xED2C2Cu);
    CHECK(reset.multi_color_hexes == "#ED2C2C,#00FF00");
    // The tag has no colour name; the user's described another colour.
    CHECK(reset.color_name.empty());
    // The catalog pick named a PLA product.
    CHECK(reset.catalog_id.empty());
    // Fields the tag does not state stay the user's.
    CHECK(reset.brand == "eSUN");
    CHECK(reset.remaining_weight_g == 400.0F);
    CHECK(tag_provenance(tag, reset).from_tag(TagField::Color));

    SECTION("a reset that moves no identity keeps the catalog pick") {
        TagReading colour_only;
        colour_only.color_rgb = 0xFF0000u;
        CHECK(reset_to_tag(edited, colour_only).catalog_id == "esun-pla-plus");
    }
}

TEST_CASE("folding a frame keeps the tag's values under our own write's echo",
          "[ams][tag_reading]") {
    const TagReading tag = petg_tag();
    const auto kept = fold_tag_frame(std::nullopt, tag, tag);
    REQUIRE(kept.has_value());
    CHECK(*kept == tag);

    SECTION("a withheld field keeps the kept value") {
        TagReading raw = tag;
        raw.material = "ABS"; // our write, echoed
        raw.color_rgb = 0x00FF00u;
        TagReading filed = raw;
        filed.material.reset();
        filed.color_rgb.reset();
        const auto folded = fold_tag_frame(kept, raw, filed);
        REQUIRE(folded.has_value());
        CHECK(folded->material == "PETG");
        CHECK(folded->color_rgb == std::optional<uint32_t>(0xED2C2Cu));
        CHECK(folded->brand == "Snapmaker");
    }
    SECTION("a field stated and filed replaces it") {
        TagReading raw = tag;
        raw.brand = "Polymaker";
        const auto folded = fold_tag_frame(kept, raw, raw);
        REQUIRE(folded.has_value());
        CHECK(folded->brand == "Polymaker");
    }
    SECTION("a field the frame no longer states is gone") {
        TagReading raw = tag;
        raw.product_name.reset();
        const auto folded = fold_tag_frame(kept, raw, raw);
        REQUIRE(folded.has_value());
        CHECK_FALSE(folded->product_name.has_value());
    }
    SECTION("nothing stated leaves no reading") {
        CHECK_FALSE(fold_tag_frame(kept, TagReading{}, TagReading{}).has_value());
    }
    SECTION("a withheld field with nothing kept is unknown, not ours") {
        TagReading raw;
        raw.material = "ABS";
        TagReading filed;
        filed.color_rgb = 0x00FF00u;
        raw.color_rgb = 0x00FF00u;
        const auto folded = fold_tag_frame(std::nullopt, raw, filed);
        REQUIRE(folded.has_value());
        CHECK_FALSE(folded->material.has_value());
        CHECK(folded->color_rgb == std::optional<uint32_t>(0x00FF00u));
    }
}

TEST_CASE("a tag naming a Spoolman spool is a reading, and a different reading",
          "[ams][tag_reading]") {
    TagReading only_id;
    only_id.spoolman_id = 42;
    CHECK_FALSE(only_id.empty());

    TagReading other_id = only_id;
    other_id.spoolman_id = 43;
    CHECK_FALSE(only_id == other_id);
    CHECK(only_id == only_id);
}

TEST_CASE("folding a frame leaves the tag's spool id to the backend", "[ams][tag_reading]") {
    TagReading kept;
    kept.material = std::string("PLA");
    kept.spoolman_id = 42;
    TagReading raw;
    raw.material = std::string("PLA");
    const auto folded = fold_tag_frame(kept, raw, raw);
    REQUIRE(folded);
    CHECK_FALSE(folded->spoolman_id);
}
