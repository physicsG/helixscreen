// Copyright (C) 2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "system/host_census.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unistd.h>

#include "../catch_amalgamated.hpp"

namespace fs = std::filesystem;
using helix::diag::CensusProcess;

namespace {

/// A throwaway machine root with a fake /proc and /etc.
struct FakeRoot {
    fs::path root;

    FakeRoot() {
        root =
            fs::temp_directory_path() / ("helix_host_census_" + std::to_string(::getpid()) + "_" +
                                         std::to_string(reinterpret_cast<uintptr_t>(this)));
        fs::remove_all(root);
        fs::create_directories(root / "proc");
        fs::create_directories(root / "etc");
    }
    ~FakeRoot() {
        std::error_code ec;
        fs::remove_all(root, ec);
    }

    void write(const fs::path& rel, const std::string& body) const {
        fs::create_directories((root / rel).parent_path());
        std::ofstream(root / rel, std::ios::binary) << body;
    }

    /// A process with NUL-separated @p argv and fds pointing at @p fd_targets.
    void process(long pid, const std::string& argv,
                 const std::vector<std::string>& fd_targets = {}) const {
        const fs::path dir = fs::path("proc") / std::to_string(pid);
        write(dir / "cmdline", argv);
        fs::create_directories(root / dir / "fd");
        int fd = 3;
        for (const auto& target : fd_targets) {
            fs::create_symlink(target, root / dir / "fd" / std::to_string(fd++));
        }
    }

    const CensusProcess* find(const std::vector<CensusProcess>& procs, long pid) const {
        auto it = std::find_if(procs.begin(), procs.end(),
                               [pid](const CensusProcess& p) { return p.pid == pid; });
        return it == procs.end() ? nullptr : &*it;
    }
};

const std::string NUL(1, '\0');

} // namespace

TEST_CASE("os_release_pretty_name reads PRETTY_NAME, quoted or not", "[bundle][1692]") {
    CHECK(helix::diag::os_release_pretty_name("NAME=Debian\nPRETTY_NAME=\"Debian GNU/Linux 12 "
                                              "(bookworm)\"\nID=debian\n") ==
          "Debian GNU/Linux 12 (bookworm)");
    CHECK(helix::diag::os_release_pretty_name("PRETTY_NAME='OpenWrt 21.02'\n") == "OpenWrt 21.02");
    CHECK(helix::diag::os_release_pretty_name("PRETTY_NAME=Buildroot") == "Buildroot");
    CHECK(helix::diag::os_release_pretty_name("NAME=Tina\n").empty());
}

TEST_CASE("parse_failed_units keeps the unit name and drops the description", "[bundle][1692]") {
    const auto units = helix::diag::parse_failed_units(
        "lightdm.service loaded failed failed Light Display Manager\n"
        "\n"
        "  makerbase-mac.service   loaded failed failed MAC setup\n");
    REQUIRE(units.size() == 2);
    CHECK(units[0] == "lightdm.service");
    CHECK(units[1] == "makerbase-mac.service");
    CHECK(helix::diag::parse_failed_units("").empty());
}

TEST_CASE("census_processes finds competing UIs and display holders only", "[bundle][1692]") {
    FakeRoot box;
    box.process(100, "python3" + NUL + "/home/pi/KlipperScreen/screen.py" + NUL);
    box.process(200, "/usr/bin/weston" + NUL + "--tty=1" + NUL, {"/dev/dri/card0", "/dev/null"});
    box.process(300, "/usr/sbin/sshd" + NUL + "-D" + NUL, {"/dev/null"});
    box.process(400, "/opt/app/fbview" + NUL, {"/dev/fb0", "/dev/fb0"});
    box.process(2, ""); // a kernel thread has no argv

    const auto procs = helix::diag::census_processes((box.root / "proc").string(), 32);

    REQUIRE(procs.size() == 3);
    const auto* ui = box.find(procs, 100);
    REQUIRE(ui != nullptr);
    CHECK(ui->name == "python3");
    CHECK(ui->reasons == std::vector<std::string>{"competing_ui:KlipperScreen"});

    const auto* weston = box.find(procs, 200);
    REQUIRE(weston != nullptr);
    CHECK(weston->name == "weston");
    CHECK(weston->reasons == std::vector<std::string>{"/dev/dri/card0"});

    const auto* fb = box.find(procs, 400);
    REQUIRE(fb != nullptr);
    CHECK(fb->reasons == std::vector<std::string>{"/dev/fb0"});

    CHECK(box.find(procs, 300) == nullptr);
}

TEST_CASE("census_processes stops at its cap", "[bundle][1692]") {
    FakeRoot box;
    for (long pid = 10; pid < 20; ++pid) {
        box.process(pid, "/usr/bin/guppyscreen" + NUL);
    }
    CHECK(helix::diag::census_processes((box.root / "proc").string(), 4).size() == 4);
}

TEST_CASE("collect_host_census reads the distro and leaves failed units unknown off-box",
          "[bundle][1692]") {
    FakeRoot box;
    box.write("etc/os-release", "PRETTY_NAME=\"Tina Linux\"\n");

    const auto census = helix::diag::collect_host_census(box.root.string());
    CHECK(census.os_pretty_name == "Tina Linux");
    CHECK_FALSE(census.failed_units.has_value());
}

TEST_CASE("systemd_is_init needs the running-systemd marker, not just the binary",
          "[bundle][1692]") {
    FakeRoot box;
    box.write("usr/bin/systemctl", "");
    CHECK_FALSE(helix::diag::systemd_is_init(box.root.string()));

    fs::create_directories(box.root / "run" / "systemd" / "system");
    CHECK(helix::diag::systemd_is_init(box.root.string()));
}

TEST_CASE("failed_units_from treats a silent failure as no answer", "[bundle][1692]") {
    CHECK_FALSE(helix::diag::failed_units_from(1, "").has_value());
    CHECK_FALSE(helix::diag::failed_units_from(127 << 8, "").has_value());
    REQUIRE(helix::diag::failed_units_from(0, "").has_value());
    CHECK(helix::diag::failed_units_from(0, "")->empty());
    CHECK(*helix::diag::failed_units_from(0, "a.service loaded failed failed A\n") ==
          std::vector<std::string>{"a.service"});
}

TEST_CASE("census_processes skips an fd table it cannot read", "[bundle][1692]") {
    FakeRoot box;
    box.process(100, "/usr/bin/guppyscreen" + NUL, {"/dev/fb0"});
    box.process(200, "/usr/bin/weston" + NUL, {"/dev/dri/card0"});
    const fs::path locked = box.root / "proc" / "100" / "fd";
    fs::permissions(locked, fs::perms::none);

    const auto procs = helix::diag::census_processes((box.root / "proc").string(), 32);
    fs::permissions(locked, fs::perms::owner_all);

    // The unreadable table costs that process its holder evidence, nothing else.
    REQUIRE(box.find(procs, 100) != nullptr);
    CHECK(box.find(procs, 100)->reasons.front() == "competing_ui:guppyscreen");
    REQUIRE(box.find(procs, 200) != nullptr);
    CHECK(box.find(procs, 200)->reasons == std::vector<std::string>{"/dev/dri/card0"});
}

TEST_CASE("competing_ui_names matches the installer's COMPETING_UIS", "[bundle][1692]") {
    std::ifstream in("scripts/lib/installer/competing_uis.sh");
    REQUIRE(in.is_open());
    std::stringstream body;
    body << in.rdbuf();
    const std::string text = body.str();
    const std::string key = "\nCOMPETING_UIS=\"";
    const auto start = text.find(key);
    REQUIRE(start != std::string::npos);
    const auto open = start + key.size();
    const auto close = text.find('"', open);
    REQUIRE(close != std::string::npos);

    std::vector<std::string> installer;
    std::istringstream words(text.substr(open, close - open));
    for (std::string w; words >> w;) {
        installer.push_back(w);
    }
    auto ours = helix::diag::competing_ui_names();
    std::sort(installer.begin(), installer.end());
    std::sort(ours.begin(), ours.end());
    CHECK(ours == installer);
}
