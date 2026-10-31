// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "crc32.h"
#include "three_mf.h"
#include "three_mf_reader.h"
#include "tinfl_inflate.h"
#include "try_reserve.h"
#include "zip_reader.h"

#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "../catch_amalgamated.hpp"

using helix::RawInflate;
using helix::three_mf::extracted_gcode_name;
using helix::three_mf::parse_slice_info;
using helix::three_mf::sliced_plates;
using helix::three_mf::SliceInfo;
using helix::zip::entry_bytes;
using helix::zip::entry_data_offset;
using helix::zip::inflate_entry_to_string;
using helix::zip::parse_central_directory;
using helix::zip::ZipEntry;
using helix::zip::ZipEntryInflater;
using helix::zip::ZipError;

namespace {

std::string fixture_dir() {
    std::string dir = __FILE__;
    const auto pos = dir.rfind("/tests/unit/");
    return pos != std::string::npos ? dir.substr(0, pos) + "/tests/fixtures/three_mf/"
                                    : "tests/fixtures/three_mf/";
}

std::vector<uint8_t> read_bytes(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

std::vector<uint8_t> archive(const char* name) {
    auto bytes = read_bytes(fixture_dir() + name);
    REQUIRE(!bytes.empty());
    return bytes;
}

std::string src_text(const char* name) {
    auto bytes = read_bytes(fixture_dir() + "src/" + name);
    REQUIRE(!bytes.empty());
    return {bytes.begin(), bytes.end()};
}

/// Parses the central directory from the last @p tail bytes of @p zip.
std::vector<ZipEntry> entries_of(const std::vector<uint8_t>& zip, size_t tail = SIZE_MAX) {
    tail = std::min(tail, zip.size());
    std::vector<ZipEntry> entries;
    ZipError err = ZipError::None;
    REQUIRE(
        parse_central_directory(zip.data() + zip.size() - tail, tail, zip.size(), entries, err));
    REQUIRE(err == ZipError::None);
    return entries;
}

const ZipEntry& entry_named(const std::vector<ZipEntry>& entries, const char* name) {
    const ZipEntry* e = helix::three_mf::find_archive_entry(entries, name);
    REQUIRE(e != nullptr);
    return *e;
}

/// Inflates @p entry from the whole archive in @p chunk-byte feeds.
std::string inflate(const std::vector<uint8_t>& zip, const ZipEntry& entry, size_t chunk,
                    ZipError& err) {
    const auto data =
        entry_data_offset(zip.data() + entry.lh_offset, zip.size() - entry.lh_offset, entry);
    REQUIRE(data.has_value());
    std::string out;
    auto sink = [&out](const uint8_t* p, size_t n) {
        out.append(reinterpret_cast<const char*>(p), n);
        return true;
    };
    ZipEntryInflater<RawInflate> inflater(entry);
    for (uint64_t at = 0; at < entry.csize; at += chunk) {
        const size_t n = static_cast<size_t>(std::min<uint64_t>(chunk, entry.csize - at));
        if (!inflater.feed(zip.data() + *data + at, n, sink)) {
            break;
        }
    }
    inflater.finish(err);
    return out;
}

uint32_t le32(const uint8_t* p) {
    return uint32_t{p[0]} | (uint32_t{p[1]} << 8) | (uint32_t{p[2]} << 16) | (uint32_t{p[3]} << 24);
}

/// Offset of the end-of-central-directory record in @p zip (no comment).
size_t eocd_of(const std::vector<uint8_t>& zip) {
    return zip.size() - 22;
}

} // namespace

TEST_CASE("crc32_update matches the reference value, fed whole or in pieces", "[zip]") {
    const char* text = "123456789";
    const auto* p = reinterpret_cast<const uint8_t*>(text);
    CHECK(helix::crc32_update(0, p, 9) == 0xCBF43926u);
    CHECK(helix::crc32_update(helix::crc32_update(0, p, 4), p + 4, 5) == 0xCBF43926u);
    CHECK(helix::crc32_update(0, p, 0) == 0u);
}

TEST_CASE("central directory parses every entry with its sizes and offsets", "[zip]") {
    const auto zip = archive("single_plate.gcode.3mf");
    const auto entries = entries_of(zip);
    REQUIRE(entries.size() == 7);
    CHECK(entries.front().name == "[Content_Types].xml");
    CHECK(entries.front().lh_offset == 0);

    const ZipEntry& png = entry_named(entries, "Metadata/plate_9_small.png");
    CHECK(png.method == 0);
    CHECK(png.csize == png.usize);
    const ZipEntry& gcode = entry_named(entries, "Metadata/plate_9.gcode");
    CHECK(gcode.method == 8);
    CHECK(gcode.usize == src_text("plate.gcode").size());
    CHECK(gcode.csize < gcode.usize);
}

TEST_CASE("the end record is found behind an archive comment", "[zip]") {
    const auto plain = entries_of(archive("single_plate.gcode.3mf"));
    const auto commented = entries_of(archive("commented.gcode.3mf"));
    REQUIRE(commented.size() == plain.size());
    CHECK(commented.back().name == plain.back().name);
    CHECK(commented.back().crc32 == plain.back().crc32);
}

TEST_CASE("an end-record signature inside the comment is not taken for the record", "[zip]") {
    auto zip = archive("single_plate.gcode.3mf");
    const size_t eocd = eocd_of(zip);
    // A 30-byte comment that opens with the end-record signature, its own
    // "comment length" field reading 0xFFFF.
    std::string comment("PK\x05\x06", 4);
    comment += std::string(26, '\xFF');
    zip[eocd + 20] = static_cast<uint8_t>(comment.size());
    zip[eocd + 21] = 0;
    zip.insert(zip.end(), comment.begin(), comment.end());

    std::vector<ZipEntry> entries;
    ZipError err = ZipError::None;
    REQUIRE(parse_central_directory(zip.data(), zip.size(), zip.size(), entries, err));
    CHECK(entries.size() == 7);
}

TEST_CASE("a tail read that misses part of the central directory says how much it needs", "[zip]") {
    const auto zip = archive("single_plate.gcode.3mf");
    std::vector<ZipEntry> entries;
    ZipError err = ZipError::None;
    uint64_t needed = 0;
    // 100 bytes holds the end record and only the last central record or two.
    REQUIRE_FALSE(parse_central_directory(zip.data() + zip.size() - 100, 100, zip.size(), entries,
                                          err, &needed));
    CHECK(err == ZipError::Truncated);
    CHECK(entries.empty());
    REQUIRE(needed > 100);
    REQUIRE(needed <= zip.size());

    const auto retried = entries_of(zip, static_cast<size_t>(needed));
    CHECK(retried.size() == 7);
}

TEST_CASE("a tail too short to hold the end record is truncated, a non-zip is refused", "[zip]") {
    const auto zip = archive("single_plate.gcode.3mf");
    std::vector<ZipEntry> entries;
    ZipError err = ZipError::None;
    REQUIRE_FALSE(
        parse_central_directory(zip.data() + zip.size() - 10, 10, zip.size(), entries, err));
    CHECK(err == ZipError::Truncated);

    const std::string text(4096, 'G');
    REQUIRE_FALSE(parse_central_directory(reinterpret_cast<const uint8_t*>(text.data()),
                                          text.size(), text.size(), entries, err));
    CHECK(err == ZipError::NotZip);
}

TEST_CASE("ZIP64 archives and entries are refused", "[zip]") {
    auto zip = archive("single_plate.gcode.3mf");
    std::vector<ZipEntry> entries;
    ZipError err = ZipError::None;

    SECTION("an end record whose offset is the ZIP64 escape") {
        const size_t e = eocd_of(zip);
        std::memset(zip.data() + e + 16, 0xFF, 4);
        REQUIRE_FALSE(parse_central_directory(zip.data(), zip.size(), zip.size(), entries, err));
        CHECK(err == ZipError::Zip64);
    }
    SECTION("an entry whose sizes are the ZIP64 escape") {
        const size_t e = eocd_of(zip);
        const size_t cd = le32(zip.data() + e + 16);
        std::memset(zip.data() + cd + 20, 0xFF, 8);
        REQUIRE_FALSE(parse_central_directory(zip.data(), zip.size(), zip.size(), entries, err));
        CHECK(err == ZipError::Zip64);
        CHECK(entries.empty());
    }
}

TEST_CASE("an encrypted entry is refused", "[zip]") {
    auto zip = archive("single_plate.gcode.3mf");
    const size_t cd = le32(zip.data() + eocd_of(zip) + 16);
    zip[cd + 8] |= 0x01; // general purpose flag bit 0: encrypted
    std::vector<ZipEntry> entries;
    ZipError err = ZipError::None;
    REQUIRE_FALSE(parse_central_directory(zip.data(), zip.size(), zip.size(), entries, err));
    CHECK(err == ZipError::Encrypted);
}

TEST_CASE("a stored entry reads straight out of its local header's span", "[zip]") {
    const auto zip = archive("single_plate.gcode.3mf");
    const auto entries = entries_of(zip);
    const ZipEntry& png = entry_named(entries, "Metadata/plate_9_small.png");

    ZipError err = ZipError::Inflate;
    const auto bytes = entry_bytes(zip.data() + png.lh_offset,
                                   static_cast<size_t>(helix::zip::entry_read_span(png)), png, err);
    CHECK(err == ZipError::None);
    REQUIRE(bytes.size() == png.csize);
    const std::string want = src_text("plate_small.png");
    CHECK(bytes == want);
    CHECK(helix::crc32_update(0, reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size()) ==
          png.crc32);

    SECTION("a span that ends inside the data is truncated") {
        entry_bytes(zip.data() + png.lh_offset, 64, png, err);
        CHECK(err == ZipError::Truncated);
    }
    SECTION("bytes that are another entry's header are refused") {
        const ZipEntry& other = entry_named(entries, "Metadata/plate_9.png");
        entry_bytes(zip.data() + other.lh_offset, 4096, png, err);
        CHECK(err == ZipError::BadLocalHeader);
    }
}

TEST_CASE("a deflated entry inflates to the original whatever the feed size", "[zip]") {
    const auto zip = archive("single_plate.gcode.3mf");
    const auto entries = entries_of(zip);
    const ZipEntry& gcode = entry_named(entries, "Metadata/plate_9.gcode");
    const std::string want = src_text("plate.gcode");
    for (size_t chunk : {size_t{1}, size_t{7}, size_t{4096}, size_t{1} << 20}) {
        CAPTURE(chunk);
        ZipError err = ZipError::Inflate;
        const std::string got = inflate(zip, gcode, chunk, err);
        CHECK(err == ZipError::None);
        CHECK(got == want);
    }
}

TEST_CASE("the inflater reports a CRC mismatch and corrupt or short streams", "[zip]") {
    auto zip = archive("single_plate.gcode.3mf");
    const auto entries = entries_of(zip);
    ZipEntry gcode = entry_named(entries, "Metadata/plate_9.gcode");
    ZipError err = ZipError::None;

    SECTION("a recorded CRC the bytes do not match") {
        gcode.crc32 ^= 1;
        inflate(zip, gcode, 4096, err);
        CHECK(err == ZipError::Crc);
    }
    SECTION("a recorded size the bytes do not match") {
        gcode.usize += 1;
        inflate(zip, gcode, 4096, err);
        CHECK(err == ZipError::Crc);
    }
    SECTION("a stream cut short") {
        gcode.csize /= 2;
        inflate(zip, gcode, 4096, err);
        CHECK(err == ZipError::Inflate);
    }
    SECTION("a corrupt stream") {
        const auto data =
            entry_data_offset(zip.data() + gcode.lh_offset, zip.size() - gcode.lh_offset, gcode);
        REQUIRE(data.has_value());
        for (size_t i = 0; i < 64; ++i) {
            zip[*data + 100 + i] ^= 0x5A;
        }
        inflate(zip, gcode, 4096, err);
        CHECK(err != ZipError::None);
    }
    SECTION("a stored entry with a flipped byte") {
        ZipEntry png = entry_named(entries, "Metadata/plate_9_small.png");
        const auto data =
            entry_data_offset(zip.data() + png.lh_offset, zip.size() - png.lh_offset, png);
        REQUIRE(data.has_value());
        zip[*data + 10] ^= 0xFF;
        inflate(zip, png, 4096, err);
        CHECK(err == ZipError::Crc);
    }
    SECTION("an unknown method") {
        gcode.method = 12; // bzip2
        inflate(zip, gcode, 4096, err);
        CHECK(err == ZipError::Method);
    }
}

TEST_CASE("a sink that stops ends the inflate with an I/O error", "[zip]") {
    const auto zip = archive("single_plate.gcode.3mf");
    const auto entries = entries_of(zip);
    const ZipEntry& gcode = entry_named(entries, "Metadata/plate_9.gcode");
    const auto data =
        entry_data_offset(zip.data() + gcode.lh_offset, zip.size() - gcode.lh_offset, gcode);
    REQUIRE(data.has_value());
    auto refuse = [](const uint8_t*, size_t) { return false; };
    ZipEntryInflater<RawInflate> inflater(gcode);
    CHECK_FALSE(inflater.feed(zip.data() + *data, static_cast<size_t>(gcode.csize), refuse));
    ZipError err = ZipError::None;
    CHECK_FALSE(inflater.finish(err));
    CHECK(err == ZipError::Io);
}

TEST_CASE("inflate_entry_to_string reads a small entry and refuses an oversized one", "[zip]") {
    const auto zip = archive("single_plate.gcode.3mf");
    const auto entries = entries_of(zip);
    const ZipEntry& info = entry_named(entries, helix::three_mf::kSliceInfoEntry);
    ZipError err = ZipError::Inflate;
    const auto bytes =
        entry_bytes(zip.data() + info.lh_offset, zip.size() - info.lh_offset, info, err);
    REQUIRE(err == ZipError::None);

    std::string xml;
    REQUIRE(inflate_entry_to_string<RawInflate>(info, bytes, 64 * 1024, xml, err));
    CHECK(xml == src_text("slice_info.config"));

    CHECK_FALSE(inflate_entry_to_string<RawInflate>(info, bytes, 100, xml, err));
    CHECK(err == ZipError::Truncated);
    CHECK(xml.empty());
}

TEST_CASE("sliced_plates finds plate 9 alone in a project of many plates", "[three_mf]") {
    const auto entries = entries_of(archive("single_plate.gcode.3mf"));
    const auto plates = sliced_plates(entries);
    REQUIRE(plates.size() == 1);
    CHECK(plates[0].index == 9);
    REQUIRE(plates[0].gcode);
    CHECK(plates[0].gcode->name == "Metadata/plate_9.gcode");
    REQUIRE(plates[0].md5);
    CHECK(plates[0].md5->name == "Metadata/plate_9.gcode.md5");
    REQUIRE(plates[0].thumb);
    CHECK(plates[0].thumb->name == "Metadata/plate_9.png");
    REQUIRE(plates[0].thumb_small);
    CHECK(plates[0].thumb_small->name == "Metadata/plate_9_small.png");
}

TEST_CASE("sliced_plates lists several plates in order, and none for a project", "[three_mf]") {
    const auto multi_entries = entries_of(archive("multi_plate.gcode.3mf"));
    const auto multi = sliced_plates(multi_entries);
    REQUIRE(multi.size() == 2);
    CHECK(multi[0].index == 2);
    CHECK(multi[1].index == 5);
    REQUIRE(multi[1].thumb);
    CHECK(multi[1].thumb->name == "Metadata/plate_5.png");

    CHECK(sliced_plates(entries_of(archive("project.3mf"))).empty());
}

TEST_CASE("plate numbers come only from plate G-code names", "[three_mf]") {
    using helix::three_mf::plate_of_gcode_entry;
    CHECK(plate_of_gcode_entry("Metadata/plate_9.gcode") == 9);
    CHECK(plate_of_gcode_entry("Metadata/plate_12.gcode") == 12);
    CHECK(plate_of_gcode_entry("Metadata/plate_9.gcode.md5") == 0);
    CHECK(plate_of_gcode_entry("Metadata/plate_.gcode") == 0);
    CHECK(plate_of_gcode_entry("Metadata/plate_x9.gcode") == 0);
    CHECK(plate_of_gcode_entry("Other/plate_9.gcode") == 0);
    CHECK(plate_of_gcode_entry("Metadata/plate_9.png") == 0);
}

TEST_CASE("parse_slice_info reads the asked-for plate's block", "[three_mf]") {
    const std::string single = src_text("slice_info.config");
    SliceInfo info;
    REQUIRE(parse_slice_info(single, 9, info));
    CHECK(info.plate == 9);
    CHECK(info.prediction_s == Catch::Approx(5238));
    CHECK(info.weight_g == Catch::Approx(23.73));
    REQUIRE(info.filaments.size() == 1);
    CHECK(info.filaments[0].id == 1);
    CHECK(info.filaments[0].type == "ABS");
    CHECK(info.filaments[0].color == "#000000");
    CHECK(info.filaments[0].used_m == Catch::Approx(9.49));
    CHECK(info.filaments[0].used_g == Catch::Approx(23.73));

    CHECK_FALSE(parse_slice_info(single, 1, info));
    CHECK_FALSE(parse_slice_info("", 9, info));
}

TEST_CASE("parse_slice_info tells a multi-plate archive's plates apart", "[three_mf]") {
    const auto zip = archive("multi_plate.gcode.3mf");
    const auto entries = entries_of(zip);
    const ZipEntry& e = entry_named(entries, helix::three_mf::kSliceInfoEntry);
    ZipError err = ZipError::None;
    const auto bytes = entry_bytes(zip.data() + e.lh_offset, zip.size() - e.lh_offset, e, err);
    std::string xml;
    REQUIRE(inflate_entry_to_string<RawInflate>(e, bytes, 64 * 1024, xml, err));

    SliceInfo two, five;
    REQUIRE(parse_slice_info(xml, 2, two));
    REQUIRE(parse_slice_info(xml, 5, five));
    CHECK(two.prediction_s == Catch::Approx(5238));
    CHECK(five.prediction_s == Catch::Approx(1800));
    CHECK(five.weight_g == Catch::Approx(12.5));
    REQUIRE(five.filaments.size() == 2);
    CHECK(five.filaments[1].type == "PLA");
    CHECK(five.filaments[1].color == "#FF0000");
    CHECK(two.filaments.size() == 1);
}

TEST_CASE("parse_slice_info reads attributes however they are spaced and quoted", "[three_mf]") {
    const std::string xml = "<config><plate>\n"
                            "<metadata value='7' key = \"index\"/>\n"
                            "<metadata key=\"prediction\"   value = '61.5' />\n"
                            "<metadataextra key=\"weight\" value=\"99\"/>\n"
                            "<filament xid=\"9\" id=\"3\" type=\"PETG\"/>\n"
                            "</plate></config>";
    SliceInfo info;
    REQUIRE(parse_slice_info(xml, 7, info));
    CHECK(info.prediction_s == Catch::Approx(61.5));
    CHECK(info.weight_g == 0); // <metadataextra> is not <metadata>
    REQUIRE(info.filaments.size() == 1);
    CHECK(info.filaments[0].id == 3);
    CHECK(info.filaments[0].type == "PETG");
}

TEST_CASE("extracted_gcode_name names one plate's G-code beside its archive", "[three_mf]") {
    CHECK(extracted_gcode_name("X.gcode.3mf", 9, false) == "X.gcode");
    CHECK(extracted_gcode_name("dir/X.gcode.3mf", 9, true) == "dir/X_plate9.gcode");
    CHECK(extracted_gcode_name("Benchy.3mf", 1, false) == "Benchy.gcode");
    CHECK(extracted_gcode_name("Benchy.3MF", 2, true) == "Benchy_plate2.gcode");
    CHECK(extracted_gcode_name("a.b/Part.gcode.3mf", 12, true) == "a.b/Part_plate12.gcode");
}

TEST_CASE("a central record whose data runs past the central directory is refused", "[zip]") {
    auto zip = archive("single_plate.gcode.3mf");
    const size_t cd = le32(zip.data() + eocd_of(zip) + 16);
    // The first record's compressed size, made to overrun everything after it.
    zip[cd + 20] = 0xFF;
    zip[cd + 21] = 0xFF;
    zip[cd + 22] = 0xFF;
    zip[cd + 23] = 0x7F;
    std::vector<ZipEntry> entries;
    ZipError err = ZipError::None;
    REQUIRE_FALSE(parse_central_directory(zip.data(), zip.size(), zip.size(), entries, err));
    CHECK(err == ZipError::NotZip);
    CHECK(entries.empty());
}

TEST_CASE("an entry larger than the caller holds is refused before it is read", "[zip]") {
    ZipEntry big;
    big.csize = 10;
    big.usize = 5000;
    CHECK_FALSE(helix::zip::entry_fits(big, 4096));
    big.usize = 10;
    big.csize = 5000;
    CHECK_FALSE(helix::zip::entry_fits(big, 4096));
    big.csize = 4096;
    big.usize = 4096;
    CHECK(helix::zip::entry_fits(big, 4096));
}

TEST_CASE("inflate_entry_to_string fails cleanly when it cannot reserve the output", "[zip]") {
    const auto zip = archive("single_plate.gcode.3mf");
    const auto entries = entries_of(zip);
    const ZipEntry& info = entry_named(entries, helix::three_mf::kSliceInfoEntry);
    ZipError err = ZipError::None;
    const auto bytes =
        entry_bytes(zip.data() + info.lh_offset, zip.size() - info.lh_offset, info, err);
    REQUIRE(err == ZipError::None);
    helix::try_reserve_fails_for_test() = true;
    std::string out;
    const bool ok = inflate_entry_to_string<RawInflate>(info, bytes, 64 * 1024, out, err);
    helix::try_reserve_fails_for_test() = false;
    CHECK_FALSE(ok);
    CHECK(err == ZipError::Inflate);
}

TEST_CASE("parse_slice_info ignores slot and plate numbers no print can have", "[three_mf]") {
    const std::string xml = "<config><plate>\n"
                            "<metadata key=\"index\" value=\"3\"/>\n"
                            "<filament id=\"1e9\" type=\"HUGE\"/>\n"
                            "<filament id=\"-5\" type=\"NEG\"/>\n"
                            "<filament id=\"2.5\" type=\"FRAC\"/>\n"
                            "<filament id=\"nan\" type=\"NAN\"/>\n"
                            "<filament id=\"2\" type=\"PLA\"/>\n"
                            "</plate><plate><metadata key=\"index\" value=\"1e12\"/></plate>"
                            "</config>";
    SliceInfo info;
    REQUIRE(parse_slice_info(xml, 3, info));
    REQUIRE(info.filaments.size() == 1);
    CHECK(info.filaments[0].id == 2);
    // A plate number that large names no plate at all.
    CHECK_FALSE(parse_slice_info(xml, 0, info));

    helix::three_mf::PlateSummary plate;
    plate.index = 3;
    plate.info = info;
    const auto meta = helix::three_mf::plate_file_metadata(plate);
    CHECK(meta.filament_colors.size() == 2); // slots 1-2, never a billion
    CHECK(helix::three_mf::plate_of_gcode_entry("Metadata/plate_99999999999.gcode") == 0);
}

// ---------------------------------------------------------------------------
// model_settings.config: plate names and the entries each plate points at
// ---------------------------------------------------------------------------

namespace {

using helix::three_mf::parse_model_settings;
using helix::three_mf::PlateSettings;

/// Central records named @p names, enough for sliced_plates() to look up.
std::vector<ZipEntry> named_entries(std::initializer_list<const char*> names) {
    std::vector<ZipEntry> entries;
    for (const char* n : names) {
        ZipEntry e;
        e.name = n;
        entries.push_back(e);
    }
    return entries;
}

PlateSettings plate_settings(int plate, std::string gcode, std::string thumb = {},
                             std::string name = {}) {
    PlateSettings s;
    s.plate = plate;
    s.gcode_file = std::move(gcode);
    s.thumbnail_file = std::move(thumb);
    s.name = std::move(name);
    return s;
}

} // namespace

TEST_CASE("parse_model_settings reads each plate's number, name and entries",
          "[three_mf][model_settings]") {
    const std::string xml = R"(<?xml version="1.0" encoding="UTF-8"?>
<config>
  <object id="2"><metadata key="name" value="not a plate"/></object>
  <plate>
    <metadata key="plater_id" value="2"/>
    <metadata key="plater_name" value="  Brackets &amp; Clips "/>
    <metadata key="filament_maps" value="1 1 2 2"/>
    <metadata key="gcode_file" value="Metadata/plate_2.gcode"/>
    <metadata key="thumbnail_file" value="Metadata/plate_2.png"/>
  </plate>
  <plate>
    <metadata key="plater_id" value="1"/>
    <metadata key="plater_name" value=""/>
    <metadata key="gcode_file" value=""/>
  </plate>
  <plate>
    <metadata key="plater_name" value="no number"/>
  </plate>
</config>)";
    const auto plates = parse_model_settings(xml);
    REQUIRE(plates.size() == 2);
    CHECK(plates[0].plate == 2);
    CHECK(plates[0].name == "Brackets & Clips");
    CHECK(plates[0].gcode_file == "Metadata/plate_2.gcode");
    CHECK(plates[0].thumbnail_file == "Metadata/plate_2.png");
    CHECK(plates[1].plate == 1);
    CHECK(plates[1].name.empty());
    CHECK(plates[1].gcode_file.empty());
}

TEST_CASE("parse_model_settings cuts a long name at a character boundary",
          "[three_mf][model_settings]") {
    // 31 ASCII bytes then a 3-byte character straddling the limit.
    const std::string name = std::string(31, 'a') + "\xE6\x9D\xBF";
    const auto plates = parse_model_settings(
        "<plate><metadata key=\"plater_id\" value=\"1\"/><metadata key=\"plater_name\" value=\"" +
        name + "\"/></plate>");
    REQUIRE(plates.size() == 1);
    CHECK(plates[0].name == std::string(31, 'a'));
    CHECK(plates[0].name.size() <= helix::three_mf::kMaxPlateNameBytes);
}

TEST_CASE("sliced_plates follows a plate's G-code pointer", "[three_mf][model_settings]") {
    const auto entries = named_entries({"Metadata/plate_1.gcode", "Metadata/plate_1.gcode.md5",
                                        "Metadata/first.gcode", "Metadata/first.gcode.md5",
                                        "Metadata/render_1.png", "Metadata/plate_1.png"});
    const auto plates = sliced_plates(
        entries, {plate_settings(1, "Metadata/first.gcode", "/Metadata/render_1.png", "Benchy")});
    REQUIRE(plates.size() == 1);
    CHECK(plates[0].index == 1);
    CHECK(plates[0].name == "Benchy");
    REQUIRE(plates[0].gcode);
    CHECK(plates[0].gcode->name == "Metadata/first.gcode");
    // The checksum is the one beside the entry the G-code comes from.
    REQUIRE(plates[0].md5);
    CHECK(plates[0].md5->name == "Metadata/first.gcode.md5");
    REQUIRE(plates[0].thumb);
    CHECK(plates[0].thumb->name == "Metadata/render_1.png");
}

TEST_CASE("sliced_plates falls back to plate names when a pointer names nothing",
          "[three_mf][model_settings]") {
    const auto entries = named_entries(
        {"Metadata/plate_1.gcode", "Metadata/plate_1.gcode.md5", "Metadata/plate_1.png"});

    SECTION("the pointed-at entries are missing") {
        const auto plates = sliced_plates(
            entries, {plate_settings(1, "Metadata/gone.gcode", "Metadata/gone.png", "Named")});
        REQUIRE(plates.size() == 1);
        REQUIRE(plates[0].gcode);
        CHECK(plates[0].gcode->name == "Metadata/plate_1.gcode");
        REQUIRE(plates[0].md5);
        CHECK(plates[0].md5->name == "Metadata/plate_1.gcode.md5");
        REQUIRE(plates[0].thumb);
        CHECK(plates[0].thumb->name == "Metadata/plate_1.png");
        CHECK(plates[0].name == "Named"); // the name still applies
    }
    SECTION("no model_settings at all") {
        const auto plates = sliced_plates(entries);
        REQUIRE(plates.size() == 1);
        CHECK(plates[0].gcode->name == "Metadata/plate_1.gcode");
        CHECK(plates[0].name.empty());
    }
    SECTION("a pointed G-code without a checksum of its own takes none") {
        const auto with_other = named_entries(
            {"Metadata/plate_1.gcode", "Metadata/plate_1.gcode.md5", "Metadata/other.gcode"});
        const auto plates = sliced_plates(with_other, {plate_settings(1, "Metadata/other.gcode")});
        REQUIRE(plates.size() == 1);
        CHECK(plates[0].gcode->name == "Metadata/other.gcode");
        CHECK(plates[0].md5 == nullptr);
    }
}

TEST_CASE("sliced_plates keeps plate order and gives a pointed entry to one plate",
          "[three_mf][model_settings]") {
    const auto entries = named_entries({"Metadata/plate_1.gcode", "Metadata/plate_2.gcode",
                                        "Metadata/plate_3.gcode", "Metadata/plate_5.gcode"});
    // model_settings lists plates out of order, and plate 4 prints plate_2's entry.
    const auto plates = sliced_plates(entries, {plate_settings(3, "Metadata/plate_3.gcode"),
                                                plate_settings(4, "Metadata/plate_2.gcode"),
                                                plate_settings(1, "Metadata/plate_1.gcode")});
    REQUIRE(plates.size() == 4);
    CHECK(plates[0].index == 1);
    CHECK(plates[1].index == 3);
    CHECK(plates[2].index == 4);
    CHECK(plates[2].gcode->name == "Metadata/plate_2.gcode");
    CHECK(plates[3].index == 5); // named, absent from model_settings
}
