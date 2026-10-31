// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../test_helpers/archive_transfers.h"
#include "../test_helpers/stored_zip.h"
#include "three_mf_reader.h"

#include <atomic>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <unistd.h>

#include "../catch_amalgamated.hpp"

using helix::three_mf::ArchiveSummary;
using helix::three_mf::ThreeMfReader;
using helix::zip::ZipError;

namespace {

std::string fixture_dir() {
    std::string dir = __FILE__;
    const auto pos = dir.rfind("/tests/unit/");
    return pos != std::string::npos ? dir.substr(0, pos) + "/tests/fixtures/three_mf/"
                                    : "tests/fixtures/three_mf/";
}

std::string disk_text(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

std::string fixture(const char* name) {
    std::string bytes = disk_text(fixture_dir() + name);
    REQUIRE(!bytes.empty());
    return bytes;
}

/// A scratch path removed when the test ends.
struct TempFile {
    std::string path;
    explicit TempFile(const char* stem) {
        path = "/tmp/helix_three_mf_test_" + std::to_string(::getpid()) + "_" + stem;
    }
    ~TempFile() {
        std::remove(path.c_str());
    }
};

void write_file(const std::string& path, const std::string& bytes) {
    std::ofstream out(path, std::ios::binary);
    out << bytes;
}

struct ReaderFixture {
    MoonrakerClientMock client{MoonrakerClientMock::PrinterType::VORON_24};
    ArchiveTransfers transfers{client, ""};
    ThreeMfReader reader{transfers};

    struct Result {
        bool done = false;
        bool ok = false;
        ArchiveSummary summary;
        std::string error;
    };

    Result summarise(const std::string& path, double modified = 1.0) {
        Result r;
        const uint64_t size = read_size(path);
        reader.read_summary(
            path, size, modified,
            [&r](const ArchiveSummary& s) {
                r.done = true;
                r.ok = true;
                r.summary = s;
            },
            [&r](const std::string& e) {
                r.done = true;
                r.error = e;
            });
        REQUIRE(r.done);
        return r;
    }

    std::map<std::string, uint64_t> sizes;
    void put(const std::string& path, std::string bytes) {
        sizes[path] = bytes.size();
        transfers.put(path, std::move(bytes));
    }
    uint64_t read_size(const std::string& path) {
        return sizes.count(path) ? sizes[path] : 0;
    }
};

} // namespace

TEST_CASE_METHOD(ReaderFixture, "a summary of one sliced plate costs one tail read",
                 "[three_mf][reader]") {
    put("a.gcode.3mf", fixture("single_plate.gcode.3mf"));
    const auto r = summarise("a.gcode.3mf");
    REQUIRE(r.ok);
    REQUIRE(r.summary.plates.size() == 1);
    const auto& plate = r.summary.plates[0];
    CHECK(plate.index == 9);
    REQUIRE(plate.has_info);
    CHECK(plate.info.prediction_s == Catch::Approx(5238));
    CHECK(plate.info.weight_g == Catch::Approx(23.73));
    REQUIRE(plate.info.filaments.size() == 1);
    CHECK(plate.info.filaments[0].type == "ABS");
    REQUIRE(plate.thumb);
    CHECK(plate.thumb->name == "Metadata/plate_9.png");
    REQUIRE(plate.thumb_small);
    // The fixture is smaller than one tail read, so slice_info came with it.
    CHECK(transfers.ranges == 1);
}

TEST_CASE_METHOD(ReaderFixture, "slice_info outside the tail read costs one more read",
                 "[three_mf][reader]") {
    put("f.gcode.3mf", fixture("far_info.gcode.3mf"));
    const auto r = summarise("f.gcode.3mf");
    REQUIRE(r.ok);
    REQUIRE(r.summary.plates.size() == 1);
    CHECK(r.summary.plates[0].has_info);
    CHECK(r.summary.plates[0].info.prediction_s == Catch::Approx(5238));
    CHECK(transfers.ranges == 2);
    // The tail plus slice_info's own span, nowhere near the 111KB archive.
    CHECK(transfers.range_bytes < 64 * 1024 + 4096);
}

TEST_CASE_METHOD(ReaderFixture, "a central directory larger than the tail read is read again whole",
                 "[three_mf][reader]") {
    put("w.gcode.3mf", fixture("wide_cd.gcode.3mf"));
    const auto r = summarise("w.gcode.3mf");
    REQUIRE(r.ok);
    REQUIRE(r.summary.plates.size() == 1);
    CHECK(r.summary.plates[0].index == 9);
    CHECK(r.summary.plates[0].has_info);
    // The tail, the whole central directory, then slice_info ahead of it.
    CHECK(transfers.ranges == 3);
}

TEST_CASE_METHOD(ReaderFixture, "several sliced plates each get their own summary",
                 "[three_mf][reader]") {
    put("m.gcode.3mf", fixture("multi_plate.gcode.3mf"));
    const auto r = summarise("m.gcode.3mf");
    REQUIRE(r.ok);
    REQUIRE(r.summary.plates.size() == 2);
    CHECK(r.summary.plates[0].index == 2);
    CHECK(r.summary.plates[0].info.prediction_s == Catch::Approx(5238));
    CHECK(r.summary.plates[1].index == 5);
    CHECK(r.summary.plates[1].info.prediction_s == Catch::Approx(1800));
    REQUIRE(r.summary.plates[1].info.filaments.size() == 2);
    CHECK(r.summary.plates[1].info.filaments[1].type == "PLA");
}

TEST_CASE_METHOD(ReaderFixture, "a project with nothing sliced summarises to no plates",
                 "[three_mf][reader]") {
    put("p.3mf", fixture("project.3mf"));
    const auto r = summarise("p.3mf");
    REQUIRE(r.ok);
    CHECK(r.summary.plates.empty());
}

TEST_CASE_METHOD(ReaderFixture, "a summary is kept until the file's size or time changes",
                 "[three_mf][reader]") {
    put("a.gcode.3mf", fixture("single_plate.gcode.3mf"));
    REQUIRE(summarise("a.gcode.3mf", 1.0).ok);
    REQUIRE(transfers.ranges == 1);
    REQUIRE(summarise("a.gcode.3mf", 1.0).ok);
    CHECK(transfers.ranges == 1);
    REQUIRE(summarise("a.gcode.3mf", 2.0).ok);
    CHECK(transfers.ranges == 2);
    reader.clear_cache();
    REQUIRE(summarise("a.gcode.3mf", 2.0).ok);
    CHECK(transfers.ranges == 3);
}

TEST_CASE_METHOD(ReaderFixture, "a file that is not a zip, or that fails to read, is an error",
                 "[three_mf][reader]") {
    put("bad.gcode.3mf", std::string(5000, 'G'));
    auto r = summarise("bad.gcode.3mf");
    CHECK_FALSE(r.ok);
    CHECK(r.error.find("not a zip") != std::string::npos);

    put("a.gcode.3mf", fixture("single_plate.gcode.3mf"));
    transfers.fail_ranges = true;
    r = summarise("a.gcode.3mf");
    CHECK_FALSE(r.ok);

    // A listing size larger than the file: the tail comes back short.
    transfers.fail_ranges = false;
    sizes["a.gcode.3mf"] += 10;
    r = summarise("a.gcode.3mf");
    CHECK_FALSE(r.ok);
}

TEST_CASE_METHOD(ReaderFixture, "a summary without the size reads a suffix and keeps nothing",
                 "[three_mf][reader]") {
    for (const char* name : {"single_plate.gcode.3mf", "far_info.gcode.3mf", "wide_cd.gcode.3mf"}) {
        CAPTURE(name);
        transfers.put(name, fixture(name)); // listed with no size
        Result r;
        reader.read_summary(
            name, 0, 0.0,
            [&r](const ArchiveSummary& s) {
                r.ok = true;
                r.summary = s;
            },
            [&r](const std::string& e) { r.error = e; });
        INFO(r.error);
        REQUIRE(r.ok);
        REQUIRE(r.summary.plates.size() == 1);
        CHECK(r.summary.plates[0].index == 9);
        CHECK(r.summary.plates[0].info.prediction_s == Catch::Approx(5238));
    }
    const int tails = transfers.tails;
    reader.read_summary(
        "single_plate.gcode.3mf", 0, 0.0, [](const ArchiveSummary&) {}, [](const std::string&) {});
    CHECK(transfers.tails == tails + 1);
}

TEST_CASE("archive_size_from_tail recovers the size from the end record", "[three_mf][zip]") {
    for (const char* name : {"single_plate.gcode.3mf", "commented.gcode.3mf"}) {
        const std::string bytes = fixture(name);
        const auto* p = reinterpret_cast<const uint8_t*>(bytes.data());
        const size_t n = std::min<size_t>(bytes.size(), 4096);
        CHECK(helix::zip::archive_size_from_tail(p + bytes.size() - n, n) == bytes.size());
    }
    const std::string text(1000, 'x');
    CHECK_FALSE(helix::zip::archive_size_from_tail(reinterpret_cast<const uint8_t*>(text.data()),
                                                   text.size()));
}

TEST_CASE_METHOD(ReaderFixture, "read_entry returns a stored thumbnail's bytes in one read",
                 "[three_mf][reader]") {
    const std::string zip = fixture("single_plate.gcode.3mf");
    put("a.gcode.3mf", zip);
    const auto r = summarise("a.gcode.3mf");
    REQUIRE(r.ok);
    const auto* thumb = helix::three_mf::plate_thumbnail(r.summary.plates[0]);
    REQUIRE(thumb);
    const int before = transfers.ranges;

    std::string got;
    std::string error;
    ThreeMfReader::read_entry(
        transfers, "a.gcode.3mf", *thumb, [&got](const std::string& b) { got = b; },
        [&error](const std::string& e) { error = e; });
    CHECK(error.empty());
    CHECK(transfers.ranges == before + 1);
    CHECK(got == disk_text(fixture_dir() + "src/plate_small.png"));
}

TEST_CASE("extract_plate writes the plate's G-code from a local archive",
          "[three_mf][reader][extract]") {
    TempFile zip("in.gcode.3mf");
    TempFile out("out.gcode");
    write_file(zip.path, fixture("multi_plate.gcode.3mf"));

    CHECK(ThreeMfReader::extract_plate(zip.path, 5, out.path) == ZipError::None);
    const std::string got = disk_text(out.path);
    std::string want = disk_text(fixture_dir() + "src/plate.gcode");
    const auto at = want.find("total layer number: 200");
    REQUIRE(at != std::string::npos);
    want.replace(at, 23, "total layer number: 90");
    CHECK(got == want);

    SECTION("a plate the archive does not hold") {
        CHECK(ThreeMfReader::extract_plate(zip.path, 3, out.path) == ZipError::Missing);
        CHECK(disk_text(out.path).empty());
    }
}

TEST_CASE("extract_plate refuses a plate whose bytes do not match its checksums",
          "[three_mf][reader][extract]") {
    TempFile zip("in.gcode.3mf");
    TempFile out("out.gcode");

    SECTION("a .md5 that names other bytes") {
        write_file(zip.path, fixture("bad_md5.gcode.3mf"));
        CHECK(ThreeMfReader::extract_plate(zip.path, 9, out.path) == ZipError::Md5);
        CHECK(disk_text(out.path).empty());
    }
    SECTION("the matching .md5 passes, upper-case hex and all") {
        write_file(zip.path, fixture("single_plate.gcode.3mf"));
        CHECK(ThreeMfReader::extract_plate(zip.path, 9, out.path) == ZipError::None);
        CHECK(disk_text(out.path) == disk_text(fixture_dir() + "src/plate.gcode"));
    }
    SECTION("a corrupt deflate stream fails the CRC or the inflate") {
        std::string bytes = fixture("single_plate.gcode.3mf");
        // The G-code's local header name: the first "plate_9.gcode" not
        // followed by ".md5". Its compressed data follows it.
        const std::string name = "Metadata/plate_9.gcode";
        size_t at = bytes.find(name);
        while (at != std::string::npos && bytes[at + name.size()] == '.') {
            at = bytes.find(name, at + 1);
        }
        REQUIRE(at != std::string::npos);
        for (size_t i = 0; i < 32; ++i) {
            bytes[at + 400 + i] ^= 0x5A;
        }
        write_file(zip.path, bytes);
        const ZipError err = ThreeMfReader::extract_plate(zip.path, 9, out.path);
        CHECK((err == ZipError::Crc || err == ZipError::Inflate));
        CHECK(disk_text(out.path).empty());
    }
    SECTION("a missing archive") {
        CHECK(ThreeMfReader::extract_plate(zip.path + ".absent", 9, out.path) == ZipError::Io);
    }
}

TEST_CASE("archive_file_metadata adds up every plate and keeps the shown plate's filaments",
          "[three_mf][reader]") {
    helix::three_mf::ArchiveSummary summary;
    helix::three_mf::PlateSummary a, b;
    a.index = 2;
    a.info.prediction_s = 600;
    a.info.weight_g = 10;
    a.info.filaments = {{1, "PLA", "#FFFFFF", 1, 10}};
    b.index = 5;
    b.info.prediction_s = 300;
    b.info.weight_g = 4;
    b.info.filaments = {{2, "PETG", "#FF0000", 1, 4}};
    summary.plates = {a, b};
    const auto meta = helix::three_mf::archive_file_metadata(summary, summary.plates[1]);
    CHECK(meta.estimated_time == Catch::Approx(900));
    CHECK(meta.filament_weight_total == Catch::Approx(14));
    CHECK(meta.filament_type == ";PETG");
    CHECK(meta.filament_colors == std::vector<std::string>{"", "#FF0000"});
}

TEST_CASE("entry_payload copies a stored entry and inflates a deflated one", "[three_mf][reader]") {
    const std::string zip = fixture("single_plate.gcode.3mf");
    std::vector<helix::zip::ZipEntry> entries;
    ZipError err = ZipError::None;
    REQUIRE(helix::zip::parse_central_directory(reinterpret_cast<const uint8_t*>(zip.data()),
                                                zip.size(), zip.size(), entries, err));
    auto from = [&zip](const helix::zip::ZipEntry& e) {
        return zip.substr(static_cast<size_t>(e.lh_offset),
                          static_cast<size_t>(helix::zip::entry_read_span(e)));
    };
    const auto* png = helix::three_mf::find_archive_entry(entries, "Metadata/plate_9.png");
    const auto* info = helix::three_mf::find_archive_entry(entries, "Metadata/slice_info.config");
    REQUIRE(png);
    REQUIRE(info);
    REQUIRE(png->method == 0);
    REQUIRE(info->method == 8); // a deflated entry, the way a deflated render arrives

    std::string out;
    CHECK(helix::three_mf::entry_payload(from(*png), *png, 1 << 20, out, err));
    CHECK(out == disk_text(fixture_dir() + "src/plate_small.png"));
    CHECK(helix::three_mf::entry_payload(from(*info), *info, 1 << 20, out, err));
    CHECK(out == disk_text(fixture_dir() + "src/slice_info.config"));
    // Larger than the caller can hold inflated: refused before it inflates.
    CHECK_FALSE(helix::three_mf::entry_payload(from(*info), *info, 100, out, err));
}

TEST_CASE_METHOD(ReaderFixture, "read_entry refuses an entry too large to hold without reading it",
                 "[three_mf][reader]") {
    put("a.gcode.3mf", fixture("single_plate.gcode.3mf"));
    helix::zip::ZipEntry huge;
    huge.name = "Metadata/plate_9.png";
    huge.csize = helix::three_mf::kMaxEntryBytes + 1;
    huge.usize = 10;
    std::string error;
    ThreeMfReader::read_entry(
        transfers, "a.gcode.3mf", huge, [](const std::string&) {},
        [&error](const std::string& e) { error = e; });
    CHECK_FALSE(error.empty());
    CHECK(transfers.ranges == 0);
}

TEST_CASE_METHOD(ReaderFixture, "the summary cache drops the least recently used archive first",
                 "[three_mf][reader]") {
    const std::string bytes = fixture("single_plate.gcode.3mf");
    auto name = [](int i) { return "f" + std::to_string(i) + ".gcode.3mf"; };
    for (int i = 0; i < 65; ++i) {
        put(name(i), bytes);
    }
    // Fill the cache, then use the first one again.
    for (int i = 0; i < 64; ++i) {
        REQUIRE(summarise(name(i)).ok);
    }
    REQUIRE(summarise(name(0)).ok);
    const int before = transfers.ranges;
    REQUIRE(summarise(name(0)).ok);
    CHECK(transfers.ranges == before); // still kept

    // One more archive: f1, now the least recently used, is the one dropped.
    REQUIRE(summarise(name(64)).ok);
    const int after_new = transfers.ranges;
    REQUIRE(summarise(name(0)).ok);
    CHECK(transfers.ranges == after_new);
    REQUIRE(summarise(name(1)).ok);
    CHECK(transfers.ranges > after_new);
}

TEST_CASE_METHOD(ReaderFixture, "a cancelled summary asks for nothing and answers nothing",
                 "[three_mf][reader]") {
    put("a.gcode.3mf", fixture("single_plate.gcode.3mf"));
    auto cancelled = std::make_shared<std::atomic<bool>>(true);
    bool answered = false;
    for (uint64_t size : {sizes["a.gcode.3mf"], uint64_t{0}}) {
        reader.read_summary(
            "a.gcode.3mf", size, 7.0, [&answered](const ArchiveSummary&) { answered = true; },
            [&answered](const std::string&) { answered = true; }, cancelled);
    }
    CHECK_FALSE(answered);
    CHECK(transfers.ranges == 0);
    CHECK(transfers.tails == 0);
}

TEST_CASE_METHOD(ReaderFixture, "a summary cancelled while a read is out answers nothing",
                 "[three_mf][reader][cancel]") {
    put("f.gcode.3mf", fixture("far_info.gcode.3mf"));
    const bool fail = GENERATE(false, true);
    // The tail read, or the slice_info read after it.
    const int held = GENERATE(1, 2);
    CAPTURE(fail, held);
    auto cancelled = std::make_shared<std::atomic<bool>>(false);
    transfers.hold_from = held;
    bool answered = false;
    reader.read_summary(
        "f.gcode.3mf", sizes["f.gcode.3mf"], 3.0,
        [&answered](const ArchiveSummary&) { answered = true; },
        [&answered](const std::string&) { answered = true; }, cancelled);
    REQUIRE(transfers.held.size() == 1);
    cancelled->store(true);
    transfers.release_held(fail);
    CHECK_FALSE(answered);

    // Nothing was kept for the file: the next read reads it, slice_info and all.
    transfers.hold_from = 0;
    const auto r = summarise("f.gcode.3mf", 3.0);
    REQUIRE(r.ok);
    REQUIRE(r.summary.plates.size() == 1);
    CHECK(r.summary.plates[0].has_info);
    CHECK(transfers.ranges == held + 2);
}

TEST_CASE_METHOD(ReaderFixture, "a slice_info read that fails answers the plates and keeps nothing",
                 "[three_mf][reader]") {
    put("f.gcode.3mf", fixture("far_info.gcode.3mf"));
    transfers.fail_from = 2;
    auto r = summarise("f.gcode.3mf", 3.0);
    REQUIRE(r.ok);
    REQUIRE(r.summary.plates.size() == 1);
    CHECK_FALSE(r.summary.plates[0].has_info);

    transfers.fail_from = 0;
    r = summarise("f.gcode.3mf", 3.0);
    REQUIRE(r.ok);
    CHECK(r.summary.plates[0].has_info);
    CHECK(transfers.ranges == 4);
}

TEST_CASE_METHOD(ReaderFixture,
                 "a summary is kept for its modified time to the fraction of a second",
                 "[three_mf][reader]") {
    put("a.gcode.3mf", fixture("single_plate.gcode.3mf"));
    REQUIRE(summarise("a.gcode.3mf", 1700000000.25).ok);
    REQUIRE(summarise("a.gcode.3mf", 1700000000.25).ok);
    CHECK(transfers.ranges == 1);
    REQUIRE(summarise("a.gcode.3mf", 1700000000.75).ok);
    CHECK(transfers.ranges == 2);
}

// ---------------------------------------------------------------------------
// model_settings.config through the reader
// ---------------------------------------------------------------------------

namespace {

const char* const kPointerSettings = R"(<config>
  <plate>
    <metadata key="plater_id" value="1"/>
    <metadata key="plater_name" value="Brackets"/>
    <metadata key="gcode_file" value="Metadata/brackets.gcode"/>
  </plate>
  <plate>
    <metadata key="plater_id" value="2"/>
    <metadata key="gcode_file" value="Metadata/missing.gcode"/>
  </plate>
</config>)";

/// Two plates: plate 1's G-code under a name model_settings points at, plate 2
/// under its plate_<N> name with a pointer to an entry that is not there.
std::string pointer_archive() {
    return helix::test::stored_zip({
        {"Metadata/brackets.gcode", "; plate one\nG28\n"},
        {"Metadata/plate_1.gcode", "; a stale plate one\n"},
        {"Metadata/plate_2.gcode", "; plate two\nG28\n"},
        {"Metadata/model_settings.config", kPointerSettings},
    });
}

} // namespace

TEST_CASE_METHOD(ReaderFixture, "a summary follows model_settings to each plate's G-code and name",
                 "[three_mf][reader][model_settings]") {
    put("p.gcode.3mf", pointer_archive());
    const auto r = summarise("p.gcode.3mf");
    REQUIRE(r.ok);
    REQUIRE(r.summary.plates.size() == 2);
    CHECK(r.summary.plates[0].index == 1);
    CHECK(r.summary.plates[0].name == "Brackets");
    REQUIRE(r.summary.plates[0].gcode);
    CHECK(r.summary.plates[0].gcode->name == "Metadata/brackets.gcode");
    CHECK(r.summary.plates[1].index == 2);
    CHECK(r.summary.plates[1].name.empty());
    REQUIRE(r.summary.plates[1].gcode);
    CHECK(r.summary.plates[1].gcode->name == "Metadata/plate_2.gcode");
    // model_settings sat inside the tail read: no read beyond it.
    CHECK(transfers.ranges == 1);
}

TEST_CASE_METHOD(ReaderFixture,
                 "a model_settings read that fails answers named plates, keeps nothing",
                 "[three_mf][reader][model_settings]") {
    // Padding ahead of the tail puts model_settings outside it.
    std::string padding(helix::zip::kZipTailReadBytes + 4096, 'x');
    put("far.gcode.3mf", helix::test::stored_zip({
                             {"Metadata/model_settings.config", kPointerSettings},
                             {"Metadata/pad.bin", padding},
                             {"Metadata/brackets.gcode", "; plate one\n"},
                             {"Metadata/plate_1.gcode", "; a stale plate one\n"},
                         }));
    transfers.fail_from = 2;
    const auto r = summarise("far.gcode.3mf");
    REQUIRE(r.ok);
    REQUIRE(r.summary.plates.size() == 1);
    CHECK(r.summary.plates[0].gcode->name == "Metadata/plate_1.gcode");
    // Not kept: the next read asks again.
    transfers.fail_from = 0;
    const auto again = summarise("far.gcode.3mf");
    REQUIRE(again.summary.plates.size() == 1);
    CHECK(again.summary.plates[0].gcode->name == "Metadata/brackets.gcode");
    CHECK(again.summary.plates[0].name == "Brackets");
}

TEST_CASE("extract_plate writes the G-code model_settings points at",
          "[three_mf][reader][extract][model_settings]") {
    TempFile zip("ptr.gcode.3mf");
    TempFile out("ptr.gcode");
    write_file(zip.path, pointer_archive());
    CHECK(ThreeMfReader::extract_plate(zip.path, 1, out.path) == ZipError::None);
    CHECK(disk_text(out.path) == "; plate one\nG28\n");
    CHECK(ThreeMfReader::extract_plate(zip.path, 2, out.path) == ZipError::None);
    CHECK(disk_text(out.path) == "; plate two\nG28\n");
}
