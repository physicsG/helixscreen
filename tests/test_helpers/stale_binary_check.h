// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <sys/wait.h>
#include <unistd.h>
#include <utility>

namespace helix::test {

/// What scripts/tree_stamp.sh reported about the tree the binary runs in.
struct TreeStampReading {
    enum class Kind {
        Unavailable, ///< not a git work tree, no script, or opted out: nothing to compare
        UnknownBase, ///< the commit the binary was built from is absent from this repository
        Measured,    ///< `stamp` holds "<base> <content-hash>" for this tree
    };
    Kind kind = Kind::Unavailable;
    std::string stamp;
};

/// The one-line warning for a binary whose link-time stamp `built` does not
/// describe the tree it now runs in, or "" when it does or cannot be judged.
///
/// `built` is "<base> <content-hash>" as the link rule recorded it, or
/// "unknown" when the link ran outside a git tree. The stamp hashes source
/// content relative to `base`, so committing edits that were already built
/// keeps the binary current, and any edit, deletion or new source file since
/// the link makes it stale.
inline std::string stale_binary_warning(std::string_view binary, std::string_view built,
                                        const TreeStampReading& now) {
    if (built.empty() || built == "unknown")
        return {};
    const auto split = [](std::string_view stamp) {
        const size_t space = stamp.find(' ');
        return std::make_pair(stamp.substr(0, space), space == std::string_view::npos
                                                          ? std::string_view{}
                                                          : stamp.substr(space + 1));
    };
    const auto [base, built_content] = split(built);
    const std::string short_base(base.substr(0, 9));
    const std::string advice = ". Run `make test` before trusting its results; "
                               "HELIX_TESTS_NO_STALE_CHECK=1 skips this check";
    switch (now.kind) {
    case TreeStampReading::Kind::Unavailable:
        return {};
    case TreeStampReading::Kind::UnknownBase:
        return "WARNING: " + std::string(binary) + " was built from commit " + short_base +
               ", which this repository does not have" + advice;
    case TreeStampReading::Kind::Measured:
        if (now.stamp == built)
            return {};
        return "WARNING: " + std::string(binary) +
               " is stale: a source it is built from changed since its build (against " +
               short_base + ": built " + std::string(built_content) + ", now " +
               std::string(split(now.stamp).second) + ")" + advice;
    }
    return {};
}

/// Run scripts/tree_stamp.sh, relative to the working directory, against the
/// commit in `built`, over the files `paths` lists (the set the link was built
/// from; empty measures every source). A run from outside a checkout (an
/// rsynced mirror, an unpacked CI artifact) finds no script, no list or no git
/// tree and reads Unavailable.
inline TreeStampReading read_tree_stamp(std::string_view built, std::string_view paths = {}) {
    TreeStampReading reading;
    const char* script = "scripts/tree_stamp.sh";
    if (built.empty() || built == "unknown" || access(script, X_OK) != 0)
        return reading;
    const std::string base(built.substr(0, built.find(' ')));
    if (base.empty() || base.find_first_not_of("0123456789abcdef") != std::string::npos)
        return reading;
    std::string cmd = script;
    if (!paths.empty()) {
        const std::string list(paths);
        if (list.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"
                                   "0123456789_./+-") != std::string::npos ||
            access(list.c_str(), R_OK) != 0)
            return reading;
        cmd += " --paths " + list;
    }
    cmd += " " + base + " 2>/dev/null";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe)
        return reading;
    char buf[256];
    std::string out;
    while (fgets(buf, sizeof(buf), pipe))
        out += buf;
    const int status = pclose(pipe);
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r'))
        out.pop_back();
    if (!WIFEXITED(status))
        return reading;
    if (WEXITSTATUS(status) == 2) {
        reading.kind = TreeStampReading::Kind::UnknownBase;
    } else if (WEXITSTATUS(status) == 0 && !out.empty()) {
        reading.kind = TreeStampReading::Kind::Measured;
        reading.stamp = out;
    }
    return reading;
}

/// Print stale_binary_warning() to stderr unless HELIX_TESTS_NO_STALE_CHECK=1.
inline void warn_if_binary_stale(std::string_view binary, std::string_view built,
                                 std::string_view paths = {}) {
    const char* opt_out = getenv("HELIX_TESTS_NO_STALE_CHECK");
    if (opt_out && std::string_view(opt_out) == "1")
        return;
    const std::string warning = stale_binary_warning(binary, built, read_tree_stamp(built, paths));
    if (!warning.empty())
        fprintf(stderr, "%s\n", warning.c_str());
}

} // namespace helix::test
