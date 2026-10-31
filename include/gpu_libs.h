// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <initializer_list>

namespace helix::gpu_libs {

/// The display and GPU libraries a DRM build can use. Where the build carries
/// them as lazy-loading trampolines (HELIX_DLOPEN_GPU_LIBS, tools/implib), the
/// binary starts without them, and every path into them asks available() first.
/// A call that gets past that into a missing library logs an error and gets a
/// failure back by return type (Failure): -1 with errno ENOSYS for a signed
/// libdrm or libinput integer and every *_get_fd*, 0.0 for a double, and 0 for
/// everything else, which is NULL, EGL_FALSE or EGL_NO_* to the caller.
enum class Lib { Drm, Gbm, Egl, Gles2, Input };

/// What the stand-in for a missing library's function returns. The values are
/// the kind codes scripts/gpu_lib_return_kinds.py writes into the build's table.
enum class Failure : char { Zero = 'z', Negative = 'i', ZeroDouble = 'd' };

/// One row of that table. Layout-compatible with the generated C struct.
struct ReturnKind {
    const char* name;
    char kind;
};

/// The failure for @p name in @p table (sorted by name, as generated); Zero
/// when the function is not listed.
Failure failure_for(const char* name, const ReturnKind* table, size_t count);

/// The stand-in function that returns @p failure.
void* stand_in(Failure failure);

/// The soname a library is loaded by. Inline so code that only names a library
/// links on hosts that build no GPU path.
inline const char* soname(Lib lib) {
    switch (lib) {
    case Lib::Drm:
        return "libdrm.so.2";
    case Lib::Gbm:
        return "libgbm.so.1";
    case Lib::Egl:
        return "libEGL.so.1";
    case Lib::Gles2:
        return "libGLESv2.so.2";
    case Lib::Input:
        return "libinput.so.10";
    }
    return "";
}

/// Whether the library loads on this system. Probed once per library and
/// cached, since the answer cannot change while the process runs; a library
/// that does not load is named in the log once. Always true in a build that
/// links the libraries directly.
bool available(Lib lib);

/// Whether every one of @p libs loads.
bool available_all(std::initializer_list<Lib> libs);

/// Answers whether a library loads. available() in the app; tests pass their own.
using LoadCheck = bool (*)(Lib);

/// Whether every library the DRM backend calls into loads: libdrm, plus GBM,
/// EGL and GLES2 when it presents through EGL.
bool drm_libs_present(bool egl_presentation, LoadCheck loads = available);

/// How the DRM backend finds and opens its input devices.
enum class InputDiscovery {
    /// libinput finds the devices; LVGL's evdev driver opens the touchscreen
    /// for multi-touch, with libinput as its fallback.
    Libinput,
    /// The sysfs scanner (input_device_scanner.h) finds the devices and LVGL's
    /// evdev driver opens them.
    SysfsEvdev,
};

/// libinput when the build carries LVGL's libinput driver and the library
/// loads, the sysfs scanner otherwise. Asks @p loads only when the driver is
/// built in, so a build without it never dlopen()s libinput.
InputDiscovery input_discovery(bool libinput_built, LoadCheck loads = available);

/// Handle from dlopen() for the library, or nullptr when it does not load.
void* handle(Lib lib);

/// Resolve a GLES2 entry point from the loaded libGLESv2, for
/// helix::gles2::load(). nullptr when the library or the symbol is missing.
void* gles2_proc(const char* name);

} // namespace helix::gpu_libs

extern "C" {
/// Implib.so's --dlopen-callback for the trampolines (mk/implib.mk): a handle
/// for @p soname, or a stand-in handle when the library does not load. Never
/// null, so the trampoline's loader never aborts.
void* helix_gpu_lib_open(const char* soname); // NAMESPACE_OK: Implib.so calls it by its C name

/// Implib.so's --dlsym-callback: the function @p name from @p lib, or a
/// stand-in that returns 0 when the library or the symbol is missing. Never
/// null, so the trampoline's resolver never aborts.
void* helix_gpu_lib_symbol(void* lib,
                           const char* name); // NAMESPACE_OK: Implib.so calls it by its C name
}
