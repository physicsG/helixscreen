// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

// Tests for system/log_path_probe.h — see that header for behavior.
//
// Why these tests exist:
//   The CC1 (Elegoo Centauri Carbon) hit a silent install failure when
//   /var/log lived on a 100% full tmpfs: open(O_CREAT|O_TRUNC) on the install
//   log succeeded (inode-only allocation), install.sh's first printf hit
//   ENOSPC, and `set -e` killed the script with a 0-byte log.  These tests
//   pin the two-stage probe (statvfs headroom + real-byte write) that
//   replaces the open-only check.

#include "system/log_path_probe.h"

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <limits>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>

#include "../catch_amalgamated.hpp"

using helix::system::probe_log_path_writable;

namespace {

// RAII scratch directory under $TMPDIR (or /tmp).  Cleaned up on destruction.
class ScratchDir {
  public:
    ScratchDir() {
        const char* base = std::getenv("TMPDIR");
        std::string templ = std::string(base ? base : "/tmp") + "/log_path_probe_XXXXXX";
        std::vector<char> buf(templ.begin(), templ.end());
        buf.push_back('\0');
        if (::mkdtemp(buf.data()) == nullptr) {
            FAIL("mkdtemp failed: " << std::strerror(errno));
        }
        path_ = buf.data();
    }
    ~ScratchDir() {
        if (path_.empty())
            return;
        // Best-effort: chmod back to 0700 in case a test made it read-only,
        // unlink any leftover files, then rmdir.
        ::chmod(path_.c_str(), 0700);
        DIR* d = ::opendir(path_.c_str());
        if (d) {
            while (auto* e = ::readdir(d)) {
                std::string name = e->d_name;
                if (name == "." || name == "..")
                    continue;
                ::unlink((path_ + "/" + name).c_str());
            }
            ::closedir(d);
        }
        ::rmdir(path_.c_str());
    }
    const std::string& path() const {
        return path_;
    }
    std::string file(const std::string& name) const {
        return path_ + "/" + name;
    }

  private:
    std::string path_;
};

} // namespace

TEST_CASE("probe_log_path_writable: happy path", "[log_path_probe]") {
    ScratchDir dir;
    const std::string log = dir.file("install.log");

    // Modest threshold — /tmp on any reasonable test host has megabytes free.
    auto result = probe_log_path_writable(log, 4096);

    REQUIRE(result.ok);
    REQUIRE(result.error.empty());

    // Probe wrote the sentinel; file exists and is non-empty.
    struct stat st {};
    REQUIRE(::stat(log.c_str(), &st) == 0);
    REQUIRE(st.st_size > 0);
}

TEST_CASE("probe_log_path_writable: rejects insufficient free space", "[log_path_probe]") {
    ScratchDir dir;
    const std::string log = dir.file("install.log");

    // Demand more bytes than any real filesystem could ever offer.
    constexpr std::uint64_t absurd = std::numeric_limits<std::uint64_t>::max() / 2;
    auto result = probe_log_path_writable(log, absurd);

    REQUIRE_FALSE(result.ok);
    REQUIRE_THAT(result.error, Catch::Matchers::ContainsSubstring("bytes free"));

    // Headroom check fails *before* open() — no stub file should exist.
    struct stat st {};
    REQUIRE(::stat(log.c_str(), &st) != 0);
}

TEST_CASE("probe_log_path_writable: open failure surfaces errno", "[log_path_probe]") {
    // Parent directory does not exist — open(O_CREAT) returns ENOENT.
    const std::string bogus = "/nonexistent_log_path_probe_xyz/install.log";

    auto result = probe_log_path_writable(bogus, 0);

    REQUIRE_FALSE(result.ok);
    REQUIRE_FALSE(result.error.empty());
    // statvfs on the missing dir fails too, so we fall through to open(),
    // which surfaces the real OS error string.
}

TEST_CASE("probe_log_path_writable: read-only directory", "[log_path_probe]") {
    if (::geteuid() == 0) {
        // Root bypasses permission bits — skip.
        SKIP("Test requires non-root euid");
    }

    ScratchDir dir;
    const std::string log = dir.file("install.log");
    REQUIRE(::chmod(dir.path().c_str(), 0500) == 0); // r-x------

    auto result = probe_log_path_writable(log, 0);

    REQUIRE_FALSE(result.ok);
    REQUIRE_FALSE(result.error.empty());
}

// ENOSPC during probe-write is hard to reproduce portably (would require
// mounting a tiny tmpfs, which needs root and a per-platform mount syntax).
// The unlink-on-failure contract is exercised by the on-device test against
// a wedged /var/log tmpfs on the CC1.
