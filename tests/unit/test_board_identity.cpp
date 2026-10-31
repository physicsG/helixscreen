// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Tests for helix::board: the one answer to "which printer board is this process on".

#include "board_identity.h"
#include "helix_test_fixture.h"
#include "platform_info.h"
#include "platform_table.h"
#include "test_helpers/flashforge_layout_fixture.h"
#include "test_helpers/scoped_board.h"
#include "test_helpers/unique_temp_dir.h"

#include <filesystem>
#include <set>
#include <string>

#include "../catch_amalgamated.hpp"

namespace fs = std::filesystem;
using helix::board::Board;
using helix::board::info;
using helix::board::resolve;
using helix::test::make_flashforge_layout;

TEST_CASE("only FlashForge firmware re-asserts brightness after boot", "[board]") {
    for (Board b : {Board::Ad5m, Board::Ad5x, Board::Creator5, Board::Creator5Pro})
        CHECK(info(b).firmware_dims_backlight_after_boot);
    for (Board b : {Board::Cc1, Board::K1, Board::K2, Board::SnapmakerU1, Board::Generic})
        CHECK_FALSE(info(b).firmware_dims_backlight_after_boot);
}

TEST_CASE("only a board whose firmware manages updates defaults to external updates", "[board]") {
    helix::set_external_updates_default_override(-1);
    for (int i = 0; i < helix::board::kBoardCount; ++i) {
        const auto b = static_cast<Board>(i);
        helix::test::ScopedBoard scoped(b);
        INFO("board: " << info(b).key);
        CHECK(helix::platform_defaults_to_external_updates() == (b == Board::SnapmakerU1));
    }
}

TEST_CASE("the AD5M has no jz_pwm piezo", "[board]") {
    CHECK_FALSE(info(Board::Ad5m).has_jz_pwm_piezo);
}

namespace {

struct TempDirGuard {
    fs::path path;
    TempDirGuard() {
        path = helix::test::unique_temp_dir("helix-board-test");
        fs::create_directories(path);
    }
    ~TempDirGuard() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

} // namespace

TEST_CASE("board::resolve names every FlashForge MIPS board from its machine id", "[board]") {
    TempDirGuard tmp;
    CHECK(resolve("mips", make_flashforge_layout(tmp.path / "a", "zmod-prog", "AD5X").string()) ==
          Board::Ad5x);
    CHECK(
        resolve("mips", make_flashforge_layout(tmp.path / "b", "zmod-prog", "Creator5").string()) ==
        Board::Creator5);
    CHECK(resolve("mips",
                  make_flashforge_layout(tmp.path / "c", "zmod-prog", "Creator5Pro").string()) ==
          Board::Creator5Pro);
}

TEST_CASE("board::resolve calls a mod tree with no machine id an AD5X and nothing at all a K1",
          "[board]") {
    TempDirGuard tmp;
    CHECK(resolve("mips", make_flashforge_layout(tmp.path, "forgex").string()) == Board::Ad5x);
    CHECK(resolve("mips", make_flashforge_layout(tmp.path, "plain").string()) == Board::K1);
}

TEST_CASE("board::resolve never probes for a compile-time board", "[board]") {
    TempDirGuard tmp;
    const std::string c5 =
        make_flashforge_layout(tmp.path / "c5", "zmod-prog", "Creator5").string();
    const std::string ad5x = make_flashforge_layout(tmp.path / "x", "zmod-prog", "AD5X").string();
    const std::string plain = make_flashforge_layout(tmp.path, "plain").string();

    CHECK(resolve("k2", c5) == Board::K2);
    CHECK(resolve("pi", ad5x) == Board::Generic);
    CHECK(resolve("ad5m", plain) == Board::Ad5m);
    CHECK(resolve("esp32", ad5x) == Board::Generic);
    CHECK(resolve("k1", ad5x) == Board::K1);
    CHECK(resolve("cc1", ad5x) == Board::Cc1);
    CHECK(resolve("snapmaker-u1", ad5x) == Board::SnapmakerU1);
    CHECK(resolve("ad5x", plain) == Board::Ad5x);
}

TEST_CASE("every Board has a row and its key round-trips", "[board]") {
    const std::set<std::string> expected = {
        "generic", "ad5m", "ad5x", "creator5", "creator5-pro", "k1", "k2", "cc1", "snapmaker-u1"};
    std::set<std::string> seen;
    for (int i = 0; i < helix::board::kBoardCount; ++i) {
        const auto b = static_cast<Board>(i);
        INFO("board index " << i);
        CHECK(info(b).board == b);
        seen.insert(info(b).key);
    }
    CHECK(seen == expected);
}

TEST_CASE("board rows carry the spec's values", "[board]") {
    // Camera probe: false on every FlashForge board, whose kernel crashes when a camera
    // streamer is woken on v4l2_open.
    for (Board b : {Board::Ad5m, Board::Ad5x, Board::Creator5, Board::Creator5Pro})
        CHECK_FALSE(info(b).local_camera_probe_safe);
    for (Board b : {Board::Generic, Board::K1, Board::K2, Board::Cc1, Board::SnapmakerU1})
        CHECK(info(b).local_camera_probe_safe);

    // The jz_pwm piezo is on the AD5X family only.
    for (Board b : {Board::Ad5x, Board::Creator5, Board::Creator5Pro})
        CHECK(info(b).has_jz_pwm_piezo);
    for (Board b :
         {Board::Generic, Board::Ad5m, Board::K1, Board::K2, Board::Cc1, Board::SnapmakerU1})
        CHECK_FALSE(info(b).has_jz_pwm_piezo);

    CHECK(info(Board::K2).reduced_effects);
    for (Board b : {Board::Generic, Board::Ad5m, Board::Ad5x, Board::Creator5, Board::Creator5Pro,
                    Board::K1, Board::Cc1, Board::SnapmakerU1})
        CHECK_FALSE(info(b).reduced_effects);

    for (Board b : {Board::Ad5m, Board::Ad5x, Board::Creator5, Board::Creator5Pro})
        CHECK(info(b).firmware_dims_backlight_after_boot);
    for (Board b : {Board::Generic, Board::K1, Board::K2, Board::Cc1, Board::SnapmakerU1})
        CHECK_FALSE(info(b).firmware_dims_backlight_after_boot);

    CHECK(info(Board::SnapmakerU1).firmware_manages_updates);
    for (Board b : {Board::Generic, Board::Ad5m, Board::Ad5x, Board::Creator5, Board::Creator5Pro,
                    Board::K1, Board::K2, Board::Cc1})
        CHECK_FALSE(info(b).firmware_manages_updates);

    CHECK(std::string(info(Board::Ad5x).legacy_release_asset) == "ad5x");
    CHECK(std::string(info(Board::K1).legacy_release_asset) == "k1");
    for (Board b : {Board::Generic, Board::Ad5m, Board::Creator5, Board::Creator5Pro, Board::K2,
                    Board::Cc1, Board::SnapmakerU1})
        CHECK(info(b).legacy_release_asset == nullptr);

    CHECK(std::string(info(Board::Generic).display_name).empty());
    CHECK(std::string(info(Board::Creator5Pro).display_name) == "FlashForge Creator 5 Pro");
    CHECK(std::string(info(Board::Cc1).display_name) == "Elegoo Centauri Carbon");
}

TEST_CASE("the override wins and a fixture resets it", "[board]") {
    const Board real = resolve(helix::platform::current_key());
    {
        HelixTestFixture fixture;
        helix::board::set_current_override(Board::Creator5Pro);
        CHECK(helix::board::current() == Board::Creator5Pro);
        CHECK(std::string(helix::board::current_info().key) == "creator5-pro");
    }
    // The fixture's destructor ran reset_all(): the next test sees the build's real board.
    CHECK(helix::board::current() == real);

    helix::board::set_current_override(Board::K2);
    HelixTestFixture next;
    CHECK(helix::board::current() == real);
}
