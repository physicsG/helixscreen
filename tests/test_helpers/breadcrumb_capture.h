// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "system/crash_handler.h"

#include <cstdio>
#include <string>
#include <unistd.h>
#include <vector>

#include "../catch_amalgamated.hpp"

namespace helix {

/// The crash handler's breadcrumb ring, one entry per line. A temp file rather
/// than a pipe: writing it never blocks, and reading it back does not wait for
/// a write end that a fork anywhere in the process could still be holding.
inline std::vector<std::string> capture_breadcrumb_lines() {
    std::FILE* f = std::tmpfile();
    REQUIRE(f != nullptr);
    const int fd = ::fileno(f);
    crash_handler::breadcrumb::dump_to_fd(fd);
    REQUIRE(::lseek(fd, 0, SEEK_SET) == 0);

    std::string all;
    char chunk[4096];
    ssize_t n;
    while ((n = ::read(fd, chunk, sizeof(chunk))) > 0) {
        all.append(chunk, static_cast<size_t>(n));
    }
    std::fclose(f);

    std::vector<std::string> lines;
    size_t pos = 0;
    size_t nl;
    while ((nl = all.find('\n', pos)) != std::string::npos) {
        lines.push_back(all.substr(pos, nl - pos));
        pos = nl + 1;
    }
    return lines;
}

} // namespace helix
