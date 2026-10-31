# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for scripts/gpu_lib_return_kinds.py: the failure each stubbed GPU-library
function returns when its library is missing, read from the libraries' headers.

A wrong kind is not caught anywhere else: the stand-in would answer 0 where a
caller reads 0 as success, or an integer where it reads a floating-point register.
"""

import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "scripts"))

from gpu_lib_return_kinds import declarations, kind_of, main, return_prefix  # noqa: E402

# Declaration shapes the real headers use: the return type on its own line
# (libinput), extern prototypes (libdrm), API macros (EGL), typedef'd pointers.
HEADER = """
/* a comment naming drmIoctl( should not count */
#define DRM_THING 1
extern int drmIoctl(int fd, unsigned long request, void *arg);
extern drmModeResPtr drmModeGetResources(int fd);
extern unsigned int drmGetVersionNumber(int fd);
extern void drmModeFreeResources(drmModeResPtr ptr);
struct gbm_device *
gbm_create_device(int fd);
int
gbm_bo_get_fd(struct gbm_bo *bo);
uint32_t
gbm_bo_get_stride(struct gbm_bo *bo);
double
libinput_event_pointer_get_dx(struct libinput_event_pointer *event);
int
libinput_dispatch(struct libinput *libinput);
enum libinput_event_type
libinput_event_get_type(struct libinput_event *event);
EGLAPI EGLBoolean EGLAPIENTRY eglInitialize (EGLDisplay dpy, EGLint *major, EGLint *minor);
EGLAPI EGLint EGLAPIENTRY eglGetError (void);
"""


def test_signed_libdrm_and_libinput_integers_fail_negative(tmp_path):
    decls = _decls(tmp_path)
    assert kind_of("drmIoctl", return_prefix(decls, "drmIoctl")) == "i"
    assert kind_of("libinput_dispatch", return_prefix(decls, "libinput_dispatch")) == "i"


def test_every_get_fd_fails_negative(tmp_path):
    decls = _decls(tmp_path)
    assert kind_of("gbm_bo_get_fd", return_prefix(decls, "gbm_bo_get_fd")) == "i"


def test_doubles_fail_with_zero_double(tmp_path):
    decls = _decls(tmp_path)
    name = "libinput_event_pointer_get_dx"
    assert kind_of(name, return_prefix(decls, name)) == "d"


def test_pointers_unsigned_enums_void_and_egl_fail_with_zero(tmp_path):
    decls = _decls(tmp_path)
    for name in ["drmModeGetResources", "gbm_create_device", "drmGetVersionNumber",
                 "drmModeFreeResources", "gbm_bo_get_stride", "libinput_event_get_type",
                 "eglInitialize", "eglGetError"]:
        assert kind_of(name, return_prefix(decls, name)) == "z", name


def test_an_undeclared_function_fails_with_zero(tmp_path):
    decls = _decls(tmp_path)
    assert return_prefix(decls, "drmNotInTheHeader") is None
    assert kind_of("drmNotInTheHeader", None) == "z"


def test_writes_a_table_sorted_by_name(tmp_path, monkeypatch):
    header = tmp_path / "h.h"
    header.write_text(HEADER)
    init_c = tmp_path / "libx.so.1.init.c"
    init_c.write_text('static const char *const sym_names[] = {\n'
                      '  "libinput_dispatch",\n  "drmModeGetResources",\n  "drmIoctl",\n  0\n};\n')
    out = tmp_path / "returns.c"
    monkeypatch.setattr(sys, "argv", ["x", "--out", str(out), "--init-c", str(init_c),
                                      "--header", str(header)])
    assert main() == 0
    rows = [line.strip() for line in out.read_text().splitlines() if line.strip().startswith('{"')]
    assert rows == ['{"drmIoctl", \'i\'},', '{"drmModeGetResources", \'z\'},',
                    '{"libinput_dispatch", \'i\'},']


def _decls(tmp_path):
    header = tmp_path / "h.h"
    header.write_text(HEADER)
    return declarations([header])
