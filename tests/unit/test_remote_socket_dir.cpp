// Copyright (C) 2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Which directory holds the `helix-screen ctl` control socket.
//
// The shipped systemd unit sets ProtectSystem=strict and declares
// RuntimeDirectory=helixscreen, so /run/helixscreen is the only writable
// directory it has and /tmp is read-only. Resolving to /tmp there fails the
// bind on every systemd install (prestonbrown/helixscreen#1602), and the
// failure is non-fatal, so the app carries on looking healthy.

#include "../test_helpers/scoped_env.h"
#include "remote_control_server.h"
#include "src/remote/unix_socket_transport.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include "../catch_amalgamated.hpp"

using helix::ClientSocketResolution;
using helix::control_socket_dir;
using helix::control_socket_search_dirs;
using helix::resolve_client_socket_path;
using helix::ScopedEnv;
using helix::well_known_socket_path;

namespace {
/// Both knobs are process-global; every case sets both so none inherits one.
struct EnvSandbox {
    ScopedEnv runtime{"RUNTIME_DIRECTORY"};
    ScopedEnv xdg{"XDG_RUNTIME_DIR"};
};

/// Unique scratch directory for one test case.
class TempDir {
  public:
    explicit TempDir(const std::string& tag) {
        path_ = "/tmp/helix-clientsock-" + tag + "-" + std::to_string(getpid());
        rmdir_recursive();
        mkdir(path_.c_str(), 0700);
    }
    ~TempDir() {
        rmdir_recursive();
    }

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    const std::string& path() const {
        return path_;
    }
    std::string well_known() const {
        return path_ + "/helixscreen-control.sock";
    }
    std::string instance(int pid) const {
        return path_ + "/helixscreen-control-" + std::to_string(pid) + ".sock";
    }

  private:
    void rmdir_recursive() const {
        std::string cmd = "rm -rf '" + path_ + "'";
        // NOLINTNEXTLINE(cert-env33-c) - fixed, self-constructed path under /tmp
        if (system(cmd.c_str()) != 0) {
            // Best effort; the directory may simply not exist yet.
        }
    }
    std::string path_;
};

/// A real listening AF_UNIX socket, closed and unlinked on destruction. What
/// `resolve_client_socket_path` calls "live" is answered only by a real
/// listener - stat() cannot tell a bound socket from a leftover file.
class Listener {
  public:
    explicit Listener(std::string path) : path_(std::move(path)) {
        fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
        REQUIRE(fd_ >= 0);
        struct sockaddr_un addr {};
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, path_.c_str(), sizeof(addr.sun_path) - 1);
        REQUIRE(bind(fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == 0);
        REQUIRE(listen(fd_, 1) == 0);
    }
    ~Listener() {
        if (fd_ >= 0) {
            close(fd_);
        }
        unlink(path_.c_str());
    }

    Listener(const Listener&) = delete;
    Listener& operator=(const Listener&) = delete;

  private:
    std::string path_;
    int fd_ = -1;
};

} // namespace

TEST_CASE("control socket dir: systemd's RUNTIME_DIRECTORY wins", "[remote][ctl][socketdir]") {
    EnvSandbox sandbox;
    setenv("RUNTIME_DIRECTORY", "/run/helixscreen", 1);
    setenv("XDG_RUNTIME_DIR", "/run/user/1000", 1);

    REQUIRE(control_socket_dir() == "/run/helixscreen");
}

TEST_CASE("control socket dir: a colon-separated list yields the first entry",
          "[remote][ctl][socketdir]") {
    EnvSandbox sandbox;
    // systemd joins multiple RuntimeDirectory= entries with ':'.
    setenv("RUNTIME_DIRECTORY", "/run/helixscreen:/run/helixscreen-extra", 1);
    unsetenv("XDG_RUNTIME_DIR");

    REQUIRE(control_socket_dir() == "/run/helixscreen");
}

TEST_CASE("control socket dir: an empty RUNTIME_DIRECTORY falls through",
          "[remote][ctl][socketdir]") {
    EnvSandbox sandbox;
    setenv("RUNTIME_DIRECTORY", "", 1);
    setenv("XDG_RUNTIME_DIR", "/run/user/1000", 1);

    REQUIRE(control_socket_dir() == "/run/user/1000");
}

TEST_CASE("control socket dir: XDG_RUNTIME_DIR is the desktop case", "[remote][ctl][socketdir]") {
    EnvSandbox sandbox;
    unsetenv("RUNTIME_DIRECTORY");
    setenv("XDG_RUNTIME_DIR", "/run/user/1000", 1);

    REQUIRE(control_socket_dir() == "/run/user/1000");
}

TEST_CASE("control socket dir: /tmp only when nothing else is set", "[remote][ctl][socketdir]") {
    EnvSandbox sandbox;
    unsetenv("RUNTIME_DIRECTORY");
    unsetenv("XDG_RUNTIME_DIR");

    REQUIRE(control_socket_dir() == "/tmp");
}

TEST_CASE("control socket dir: the well-known path sits inside it", "[remote][ctl][socketdir]") {
    EnvSandbox sandbox;
    setenv("RUNTIME_DIRECTORY", "/run/helixscreen", 1);
    unsetenv("XDG_RUNTIME_DIR");

    REQUIRE(well_known_socket_path() == "/run/helixscreen/helixscreen-control.sock");
}

TEST_CASE("resolve_socket_path: the server binds inside RUNTIME_DIRECTORY",
          "[remote][ctl][socketdir]") {
    EnvSandbox sandbox;
    // A directory that exists: resolve_socket_path() sweeps it for the sockets
    // of instances that died without teardown before it picks a path.
    const std::string dir = "/tmp/helix-rd-" + std::to_string(getpid());
    mkdir(dir.c_str(), 0700);
    setenv("RUNTIME_DIRECTORY", dir.c_str(), 1);
    unsetenv("XDG_RUNTIME_DIR");

    REQUIRE(helix::resolve_socket_path("") == dir + "/helixscreen-control.sock");
    // An explicit --remote-socket still outranks everything.
    REQUIRE(helix::resolve_socket_path("/tmp/explicit.sock") == "/tmp/explicit.sock");

    rmdir(dir.c_str());
}

// --- client-side search order --------------------------------------------

TEST_CASE("socket search: a client covers both contexts at once", "[remote][ctl][socketdir]") {
    EnvSandbox sandbox;
    // An ssh session's environment: XDG is set, RUNTIME_DIRECTORY is not. The
    // service it wants to reach bound /run/helixscreen, so a search that stopped
    // at the first resolved directory would miss it entirely.
    unsetenv("RUNTIME_DIRECTORY");
    setenv("XDG_RUNTIME_DIR", "/run/user/1000", 1);

    const auto dirs = control_socket_search_dirs();
    REQUIRE(std::find(dirs.begin(), dirs.end(), "/run/user/1000") != dirs.end());
    REQUIRE(std::find(dirs.begin(), dirs.end(), "/run/helixscreen") != dirs.end());
    REQUIRE(std::find(dirs.begin(), dirs.end(), "/tmp") != dirs.end());
}

TEST_CASE("socket search: the server's own directory is tried first", "[remote][ctl][socketdir]") {
    EnvSandbox sandbox;
    setenv("RUNTIME_DIRECTORY", "/run/helixscreen", 1);
    setenv("XDG_RUNTIME_DIR", "/run/user/1000", 1);

    const auto dirs = control_socket_search_dirs();
    REQUIRE_FALSE(dirs.empty());
    // Asserted against the literal, not against control_socket_dir() itself: if that
    // function's own preference ever inverted, comparing against its result would
    // move both sides together and this case would stay green either way.
    REQUIRE(dirs.front() == "/run/helixscreen");
}

TEST_CASE("socket search: no directory is probed twice", "[remote][ctl][socketdir]") {
    EnvSandbox sandbox;
    // Both variables naming the same place, which is what a systemd unit run by
    // a logged-in user looks like.
    setenv("RUNTIME_DIRECTORY", "/tmp", 1);
    setenv("XDG_RUNTIME_DIR", "/tmp", 1);

    const auto dirs = control_socket_search_dirs();
    auto sorted = dirs;
    std::sort(sorted.begin(), sorted.end());
    REQUIRE(std::adjacent_find(sorted.begin(), sorted.end()) == sorted.end());
}

// --- resolve_client_socket_path: what the client actually connects to -----
//
// This is the search remote_client.cpp's resolve_socket_path calls; it is
// exercised directly here because remote_client.o is excluded from the test
// link (see mk/tests.mk), so nothing else can reach it.

TEST_CASE("resolve_client_socket_path: nothing live anywhere reports NotRunning",
          "[remote][ctl][socketdir][slow]") {
    EnvSandbox sandbox;
    setenv("RUNTIME_DIRECTORY", "/run/helixscreen", 1);
    unsetenv("XDG_RUNTIME_DIR");
    TempDir dir("none");

    const auto result = resolve_client_socket_path({dir.path()});
    REQUIRE(result.status == ClientSocketResolution::Status::NotRunning);
    REQUIRE(result.path == well_known_socket_path());
    REQUIRE(result.candidates.empty());
}

TEST_CASE("resolve_client_socket_path: one live well-known socket is used automatically",
          "[remote][ctl][socketdir][slow]") {
    TempDir a("wk-a"), b("wk-b");
    Listener live(b.well_known());

    const auto result = resolve_client_socket_path({a.path(), b.path()});
    REQUIRE(result.status == ClientSocketResolution::Status::Found);
    REQUIRE(result.path == b.well_known());
}

TEST_CASE("resolve_client_socket_path: two live well-known sockets in different "
          "directories are refused, not silently preferred",
          "[remote][ctl][socketdir][slow]") {
    // The systemd-plus-ssh-session collision from prestonbrown/helixscreen#1602: two
    // unrelated apps, each the first (and only) instance in its own directory, so
    // each holds the well-known name rather than falling back to a pid-suffixed one.
    TempDir a("wk-multi-a"), b("wk-multi-b");
    Listener live_a(a.well_known());
    Listener live_b(b.well_known());

    const auto result = resolve_client_socket_path({a.path(), b.path()});
    REQUIRE(result.status == ClientSocketResolution::Status::Ambiguous);
    REQUIRE(result.candidates.size() == 2);
    REQUIRE(std::find(result.candidates.begin(), result.candidates.end(), a.well_known()) !=
            result.candidates.end());
    REQUIRE(std::find(result.candidates.begin(), result.candidates.end(), b.well_known()) !=
            result.candidates.end());
}

TEST_CASE("resolve_client_socket_path: falls back to a single pid-suffixed instance "
          "when no well-known socket is live",
          "[remote][ctl][socketdir][slow]") {
    TempDir dir("instance-one");
    Listener instance(dir.instance(getpid()));

    const auto result = resolve_client_socket_path({dir.path()});
    REQUIRE(result.status == ClientSocketResolution::Status::Found);
    REQUIRE(result.path == dir.instance(getpid()));
}

TEST_CASE("resolve_client_socket_path: several pid-suffixed instances are refused",
          "[remote][ctl][socketdir][slow]") {
    TempDir dir("instance-many");
    Listener a(dir.instance(11111));
    Listener b(dir.instance(22222));

    const auto result = resolve_client_socket_path({dir.path()});
    REQUIRE(result.status == ClientSocketResolution::Status::Ambiguous);
    REQUIRE(result.candidates.size() == 2);
}

TEST_CASE("resolve_client_socket_path: a well-known socket in one directory outranks "
          "an instance socket in another",
          "[remote][ctl][socketdir][slow]") {
    // A pid-suffixed instance implies its OWN directory's well-known name is already
    // taken, not that it is a second, independent app competing with one in another
    // directory - so the well-known search across every directory settles this
    // before the instance search ever runs.
    TempDir a("precedence-wk"), b("precedence-inst");
    Listener well_known(a.well_known());
    Listener instance(b.instance(getpid()));

    const auto result = resolve_client_socket_path({a.path(), b.path()});
    REQUIRE(result.status == ClientSocketResolution::Status::Found);
    REQUIRE(result.path == a.well_known());
}
