// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
// TEST_MIRROR_OK: the code under test is the test binary's own startup check
// (tests/test_helpers/stale_binary_check.h), which ships in no product.

#include "scope_exit.h"
#include "test_helpers/scoped_env.h"
#include "test_helpers/stale_binary_check.h"
#include "test_helpers/unique_temp_dir.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

#include "catch_amalgamated.hpp"

using helix::test::read_tree_stamp;
using helix::test::stale_binary_warning;
using helix::test::TreeStampReading;
using helix::test::warn_if_binary_stale;

namespace {

constexpr const char* kBuilt = "a54377f64211b56c1e215f174a47a461ea8e80df clean";

TreeStampReading measured(const std::string& stamp) {
    return {TreeStampReading::Kind::Measured, stamp};
}

/// A scratch directory holding a fake scripts/tree_stamp.sh with the given
/// body, made the working directory until the scope ends.
class FakeTreeStampDir {
  public:
    explicit FakeTreeStampDir(const std::string& body)
        : dir_(helix::test::unique_temp_dir("stale_binary_check")),
          old_cwd_(std::filesystem::current_path()) {
        std::filesystem::create_directories(dir_ + "/scripts");
        if (!body.empty()) {
            const std::string script = dir_ + "/scripts/tree_stamp.sh";
            std::ofstream(script) << "#!/bin/sh\n" << body << "\n";
            std::filesystem::permissions(script, std::filesystem::perms::owner_all);
        }
        std::filesystem::current_path(dir_);
    }
    ~FakeTreeStampDir() {
        std::error_code ec;
        std::filesystem::current_path(old_cwd_, ec);
        std::filesystem::remove_all(dir_, ec);
    }
    FakeTreeStampDir(const FakeTreeStampDir&) = delete;
    FakeTreeStampDir& operator=(const FakeTreeStampDir&) = delete;

  private:
    std::string dir_;
    std::filesystem::path old_cwd_;
};

/// Everything warn_if_binary_stale() writes to stderr while `fn` runs.
template <typename Fn> std::string capture_stderr(Fn fn) {
    const std::string path = helix::test::unique_temp_file("stale_binary_stderr", "txt");
    fflush(stderr);
    const int saved = dup(fileno(stderr));
    REQUIRE(saved >= 0);
    REQUIRE(freopen(path.c_str(), "w", stderr) != nullptr);
    {
        helix::ScopeExit restore([&] {
            fflush(stderr);
            dup2(saved, fileno(stderr));
            close(saved);
        });
        fn();
    }
    std::ifstream in(path);
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::filesystem::remove(path);
    return text;
}

} // namespace

TEST_CASE("stale binary warning: a tree that matches the stamp is silent", "[stale_binary]") {
    CHECK(stale_binary_warning("helix-tests", kBuilt, measured(kBuilt)).empty());
}

TEST_CASE("stale binary warning: changed sources since the link warn once, naming both states",
          "[stale_binary]") {
    const std::string w = stale_binary_warning(
        "helix-tests", kBuilt, measured("a54377f64211b56c1e215f174a47a461ea8e80df 3f2a9c1e"));
    REQUIRE_FALSE(w.empty());
    CHECK(w.rfind("WARNING: helix-tests is stale", 0) == 0);
    CHECK(w.find("a source it is built from changed") != std::string::npos);
    CHECK(w.find("built clean, now 3f2a9c1e") != std::string::npos);
    CHECK(w.find("a54377f64") != std::string::npos);
    CHECK(w.find("make test") != std::string::npos);
    CHECK(w.find("HELIX_TESTS_NO_STALE_CHECK=1") != std::string::npos);
    CHECK(w.find('\n') == std::string::npos);
}

TEST_CASE("stale binary warning: a build commit this repository lacks warns", "[stale_binary]") {
    const std::string w =
        stale_binary_warning("helix-tests", kBuilt, {TreeStampReading::Kind::UnknownBase, ""});
    REQUIRE_FALSE(w.empty());
    CHECK(w.find("commit a54377f64, which this repository does not have") != std::string::npos);
}

TEST_CASE("stale binary warning: no stamp or no measurement is never a warning", "[stale_binary]") {
    CHECK(stale_binary_warning("helix-tests", kBuilt, {}).empty());
    CHECK(stale_binary_warning("helix-tests", "unknown", measured(kBuilt)).empty());
    CHECK(stale_binary_warning("helix-tests", "", measured(kBuilt)).empty());
    CHECK(stale_binary_warning("helix-tests", "unknown", {TreeStampReading::Kind::UnknownBase, ""})
              .empty());
}

TEST_CASE("read_tree_stamp measures against the commit the binary recorded", "[stale_binary]") {
    FakeTreeStampDir dir("echo \"$1 from-script\"");
    const TreeStampReading r = read_tree_stamp(kBuilt);
    REQUIRE(r.kind == TreeStampReading::Kind::Measured);
    CHECK(r.stamp == "a54377f64211b56c1e215f174a47a461ea8e80df from-script");
}

TEST_CASE("read_tree_stamp hands the script the binary's source list", "[stale_binary]") {
    FakeTreeStampDir dir("echo \"$*\"");
    std::ofstream("link.paths") << "src/a.cpp\n";
    const TreeStampReading r = read_tree_stamp(kBuilt, "link.paths");
    REQUIRE(r.kind == TreeStampReading::Kind::Measured);
    CHECK(r.stamp == "--paths link.paths a54377f64211b56c1e215f174a47a461ea8e80df");
}

TEST_CASE("read_tree_stamp without the source list has nothing to compare", "[stale_binary]") {
    FakeTreeStampDir dir("echo \"$1 ran\"");
    CHECK(read_tree_stamp(kBuilt, "missing.paths").kind == TreeStampReading::Kind::Unavailable);
    std::ofstream("odd name.paths") << "src/a.cpp\n";
    CHECK(read_tree_stamp(kBuilt, "odd name.paths").kind == TreeStampReading::Kind::Unavailable);
}

TEST_CASE("read_tree_stamp maps the script's exit status", "[stale_binary]") {
    SECTION("exit 2: base unknown") {
        FakeTreeStampDir dir("exit 2");
        CHECK(read_tree_stamp(kBuilt).kind == TreeStampReading::Kind::UnknownBase);
    }
    SECTION("exit 1: not a git work tree") {
        FakeTreeStampDir dir("echo noise; exit 1");
        CHECK(read_tree_stamp(kBuilt).kind == TreeStampReading::Kind::Unavailable);
    }
    SECTION("no script beside the working directory") {
        FakeTreeStampDir dir("");
        CHECK(read_tree_stamp(kBuilt).kind == TreeStampReading::Kind::Unavailable);
    }
    SECTION("a base that is not a hex commit never reaches the shell") {
        FakeTreeStampDir dir("echo \"$1 ran\"");
        CHECK(read_tree_stamp("$(true) clean").kind == TreeStampReading::Kind::Unavailable);
        CHECK(read_tree_stamp("unknown").kind == TreeStampReading::Kind::Unavailable);
    }
}

TEST_CASE("warn_if_binary_stale prints to stderr unless opted out", "[stale_binary]") {
    FakeTreeStampDir dir("echo \"$1 edited\"");
    SECTION("stale tree warns") {
        helix::ScopedEnv env("HELIX_TESTS_NO_STALE_CHECK", nullptr);
        const std::string err = capture_stderr([] { warn_if_binary_stale("helix-tests", kBuilt); });
        CHECK(err.find("WARNING: helix-tests is stale") != std::string::npos);
    }
    SECTION("HELIX_TESTS_NO_STALE_CHECK=1 silences it") {
        helix::ScopedEnv env("HELIX_TESTS_NO_STALE_CHECK", "1");
        const std::string err = capture_stderr([] { warn_if_binary_stale("helix-tests", kBuilt); });
        CHECK(err.empty());
    }
}
