// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "bench_gcode_cmd.h"

#include <cstdio>
#include <fstream>
#include <string>
#include <unistd.h>

#include "../catch_amalgamated.hpp"

namespace {

std::string write_two_layer_file() {
    char path[] = "/tmp/bench_gcode_XXXXXX";
    const int fd = mkstemp(path);
    REQUIRE(fd != -1);
    close(fd);
    std::ofstream out(path);
    out << "G1 Z0.2 F600\nG1 X10 Y10 E1\nG1 X20 Y10 E2\n"
           "G1 Z0.4 F600\nG1 X10 Y20 E3\nG1 X20 Y20 E4\n";
    return path;
}

int run(std::initializer_list<const char*> args) {
    std::vector<char*> argv;
    for (const char* a : args) {
        argv.push_back(const_cast<char*>(a));
    }
    return helix::bench::run_bench_gcode(static_cast<int>(argv.size()), argv.data());
}

} // namespace

TEST_CASE("bench-gcode indexes and parses a real file", "[bench_gcode]") {
    const std::string path = write_two_layer_file();
    CHECK(run({"helix-screen", "bench-gcode", path.c_str(), "200", "150"}) == 0);
    std::remove(path.c_str());
}

TEST_CASE("bench-gcode refuses a file it cannot index", "[bench_gcode]") {
    CHECK(run({"helix-screen", "bench-gcode", "/nonexistent/none.gcode"}) == 1);
}

TEST_CASE("bench-gcode without a file prints usage", "[bench_gcode]") {
    CHECK(run({"helix-screen", "bench-gcode"}) == 2);
}
