// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gpu_libs.h"
#include "probe_egl_cmd.h"

#include <cerrno>
#include <cstring>
#include <dlfcn.h>
#include <initializer_list>
#include <string>

#include "../catch_amalgamated.hpp"

using helix::gpu_libs::Failure;
using helix::gpu_libs::InputDiscovery;
using helix::gpu_libs::Lib;
using helix::gpu_libs::ReturnKind;

namespace {

/// The loader under test answers the Implib.so trampolines, which only an ELF
/// build has, and leans on glibc's sonames; a macOS build never calls it.
void skip_without_elf_loader() {
#if defined(__APPLE__)
    SKIP("gpu lib loader is ELF/glibc only");
#endif
}

using IntFn = int (*)();
using PtrFn = void* (*)();
using DoubleFn = double (*)();
using CosFn = double (*)(double);

// Sorted by name, as scripts/gpu_lib_return_kinds.py writes the build's table.
const ReturnKind kTable[] = {
    {"drmIoctl", 'i'},
    {"drmModeGetResources", 'z'},
    {"gbm_bo_get_fd", 'i'},
    {"libinput_event_pointer_get_dx", 'd'},
};

// A stand-in for gpu_libs::available(): the libraries in g_missing do not
// load, and every library asked about is recorded in g_asked.
unsigned g_missing = 0;
unsigned g_asked = 0;

unsigned bit(Lib lib) {
    return 1U << static_cast<unsigned>(lib);
}

bool fake_loads(Lib lib) {
    g_asked |= bit(lib);
    return (g_missing & bit(lib)) == 0;
}

void set_missing(std::initializer_list<Lib> libs) {
    g_missing = 0;
    g_asked = 0;
    for (Lib lib : libs) {
        g_missing |= bit(lib);
    }
}

} // namespace

TEST_CASE("Each function's failure comes from the return-kind table", "[gpu_libs]") {
    const size_t n = std::size(kTable);
    CHECK(helix::gpu_libs::failure_for("drmIoctl", kTable, n) == Failure::Negative);
    CHECK(helix::gpu_libs::failure_for("drmModeGetResources", kTable, n) == Failure::Zero);
    CHECK(helix::gpu_libs::failure_for("gbm_bo_get_fd", kTable, n) == Failure::Negative);
    CHECK(helix::gpu_libs::failure_for("libinput_event_pointer_get_dx", kTable, n) ==
          Failure::ZeroDouble);
    // Not in the table: 0 / NULL.
    CHECK(helix::gpu_libs::failure_for("eglGetDisplay", kTable, n) == Failure::Zero);
    CHECK(helix::gpu_libs::failure_for("aaa", kTable, n) == Failure::Zero);
    CHECK(helix::gpu_libs::failure_for("zzz", kTable, n) == Failure::Zero);
}

TEST_CASE("A signed libdrm/libinput call or a *_get_fd* fails with -1 and ENOSYS", "[gpu_libs]") {
    errno = 0;
    auto fn = reinterpret_cast<IntFn>(helix::gpu_libs::stand_in(Failure::Negative));
    CHECK(fn() == -1);
    CHECK(errno == ENOSYS);
}

TEST_CASE("A double-returning call fails with 0.0", "[gpu_libs]") {
    auto fn = reinterpret_cast<DoubleFn>(helix::gpu_libs::stand_in(Failure::ZeroDouble));
    CHECK(fn() == 0.0);
}

TEST_CASE("A pointer or EGL call fails with NULL / 0", "[gpu_libs]") {
    void* zero = helix::gpu_libs::stand_in(Failure::Zero);
    CHECK(reinterpret_cast<PtrFn>(zero)() == nullptr);
    CHECK(reinterpret_cast<IntFn>(zero)() == 0);
}

TEST_CASE("A missing GPU library gets a handle, not an abort", "[gpu_libs]") {
    skip_without_elf_loader();
    void* lib = helix_gpu_lib_open("libhelix-does-not-exist.so.9");
    REQUIRE(lib != nullptr);
    dlclose(lib);
}

TEST_CASE("A missing library's handle never resolves another library's function", "[gpu_libs]") {
    skip_without_elf_loader();
    // strlen is loaded in this process; a search of the missing library's
    // handle must not find it.
    void* lib = helix_gpu_lib_open("libhelix-does-not-exist.so.9");
    REQUIRE(lib != nullptr);
    void* fn = helix_gpu_lib_symbol(lib, "strlen");
    REQUIRE(fn != nullptr);
    CHECK(fn != dlsym(RTLD_DEFAULT, "strlen"));
    CHECK(fn == helix::gpu_libs::stand_in(Failure::Zero));
    dlclose(lib);
}

TEST_CASE("A library that loads resolves its real functions", "[gpu_libs]") {
    skip_without_elf_loader();
    void* libm = helix_gpu_lib_open("libm.so.6");
    REQUIRE(libm != nullptr);
    void* fn = helix_gpu_lib_symbol(libm, "cos");
    REQUIRE(fn != nullptr);
    CHECK(reinterpret_cast<CosFn>(fn)(0.0) == 1.0);
    // A name that library lacks falls back to a stand-in.
    CHECK(helix_gpu_lib_symbol(libm, "helix_no_such_function") ==
          helix::gpu_libs::stand_in(Failure::Zero));
    dlclose(libm);
}

TEST_CASE("Every GPU library has its soname", "[gpu_libs]") {
    using helix::gpu_libs::Lib;
    CHECK(std::strcmp(helix::gpu_libs::soname(Lib::Drm), "libdrm.so.2") == 0);
    CHECK(std::strcmp(helix::gpu_libs::soname(Lib::Gbm), "libgbm.so.1") == 0);
    CHECK(std::strcmp(helix::gpu_libs::soname(Lib::Egl), "libEGL.so.1") == 0);
    CHECK(std::strcmp(helix::gpu_libs::soname(Lib::Gles2), "libGLESv2.so.2") == 0);
    CHECK(std::strcmp(helix::gpu_libs::soname(Lib::Input), "libinput.so.10") == 0);
}

TEST_CASE("The DRM backend needs libdrm, and the GPU stack only when it presents through EGL",
          "[gpu_libs]") {
    using helix::gpu_libs::drm_libs_present;
    set_missing({});
    CHECK(drm_libs_present(false, fake_loads));
    CHECK(drm_libs_present(true, fake_loads));

    set_missing({Lib::Drm});
    CHECK_FALSE(drm_libs_present(false, fake_loads));
    CHECK_FALSE(drm_libs_present(true, fake_loads));

    for (Lib gpu : {Lib::Gbm, Lib::Egl, Lib::Gles2}) {
        CAPTURE(helix::gpu_libs::soname(gpu));
        set_missing({gpu});
        CHECK(drm_libs_present(false, fake_loads));
        CHECK_FALSE(drm_libs_present(true, fake_loads));
    }

    // Dumb buffers never load the GPU stack, and nothing asks about libinput.
    set_missing({});
    REQUIRE(drm_libs_present(false, fake_loads));
    CHECK(g_asked == bit(Lib::Drm));
    REQUIRE(drm_libs_present(true, fake_loads));
    CHECK((g_asked & bit(Lib::Input)) == 0);
}

TEST_CASE("Input devices come from libinput only when it is built in and loads", "[gpu_libs]") {
    using helix::gpu_libs::input_discovery;
    set_missing({});
    CHECK(input_discovery(true, fake_loads) == InputDiscovery::Libinput);

    set_missing({Lib::Input});
    CHECK(input_discovery(true, fake_loads) == InputDiscovery::SysfsEvdev);

    // A build without LVGL's libinput driver never dlopen()s the library.
    set_missing({});
    CHECK(input_discovery(false, fake_loads) == InputDiscovery::SysfsEvdev);
    CHECK(g_asked == 0);

    // The GPU libraries have no say in it.
    set_missing({Lib::Drm, Lib::Gbm, Lib::Egl, Lib::Gles2});
    CHECK(input_discovery(true, fake_loads) == InputDiscovery::Libinput);
}

TEST_CASE("--probe-egl's verdict before it touches the GPU", "[gpu_libs][probe_egl]") {
    using helix::probe::kProbeDeclined;
    using helix::probe::kProbeNoDrm;
    using helix::probe::kProbeNoRungs;
    using helix::probe::probe_gate;

    SECTION("a binary without the display rungs leaves the backend to auto-detection") {
        set_missing({Lib::Drm});
        const auto gate = probe_gate(false, true, fake_loads);
        CHECK_FALSE(gate.probe);
        CHECK(gate.status == kProbeNoRungs);
        CHECK(g_asked == 0);
    }

    SECTION("no libdrm means fbdev, even with the rest of the GPU stack missing too") {
        set_missing({Lib::Drm, Lib::Gbm, Lib::Egl, Lib::Gles2, Lib::Input});
        const auto gate = probe_gate(true, true, fake_loads);
        CHECK_FALSE(gate.probe);
        CHECK(gate.status == kProbeNoDrm);
        CHECK(gate.reason == "libdrm.so.2 is not installed");
        // Without EGL presentation linked in, no libdrm still wins.
        CHECK(probe_gate(true, false, fake_loads).status == kProbeNoDrm);
    }

    SECTION("a binary without EGL presentation declines to DRM dumb buffers") {
        set_missing({});
        const auto gate = probe_gate(true, false, fake_loads);
        CHECK_FALSE(gate.probe);
        CHECK(gate.status == kProbeDeclined);
        CHECK(gate.reason == "built without EGL presentation");
    }

    SECTION("a missing GBM, EGL or GLES2 declines to DRM dumb buffers") {
        for (Lib gpu : {Lib::Gbm, Lib::Egl, Lib::Gles2}) {
            set_missing({gpu});
            const auto gate = probe_gate(true, true, fake_loads);
            CHECK_FALSE(gate.probe);
            CHECK(gate.status == kProbeDeclined);
            CHECK(gate.reason == std::string(helix::gpu_libs::soname(gpu)) + " is not installed");
        }
    }

    SECTION("everything present goes on to the GPU probe; libinput has no say") {
        set_missing({Lib::Input});
        const auto gate = probe_gate(true, true, fake_loads);
        CHECK(gate.probe);
    }
}
