# SPDX-License-Identifier: GPL-3.0-or-later
"""
Capability probes for the HelixPrint plugin.

Generic plugin code asks capability questions and never names a vendor:
"does this Moonraker print .3mf natively?" Each question is answered here by
a list of probes. The generic probe reads what any Moonraker exposes;
vendor-specific probes, each named for its vendor, recognise a firmware fork
by the code it adds. A vendor's knowledge lives in its probe and nowhere else.

A probe answers True (has the capability), False (does not) or None (cannot
tell). A vendor probe recognises its fork and so only ever says True or None.

The installer's Klipper question ("does Klipper hide dot-directories from
SDCARD_PRINT_FILE?") is answered on the shell side, in install.sh's
klipper_hides_dot_dirs, with the same generic/vendor split.
"""

from __future__ import annotations

import sys
from typing import Any, Callable, Iterable, Optional

ProbeAnswer = Optional[bool]
NativeFormatProbe = Callable[[Any], ProbeAnswer]


def _file_manager_module(file_manager: Any) -> Any:
    return sys.modules.get(type(file_manager).__module__)


# Moonraker's VALID_GCODE_EXTS where its module cannot be read.
DEFAULT_PRINT_EXTS = (".gcode", ".g", ".gco", ".ufp", ".nc")


def print_extensions(file_manager: Any) -> tuple:
    """The extensions Moonraker lists as G-code and starts on an upload's
    print=true: its file manager module's VALID_GCODE_EXTS, lower-cased."""
    exts = getattr(_file_manager_module(file_manager), "VALID_GCODE_EXTS", None)
    if not exts:
        return DEFAULT_PRINT_EXTS
    return tuple(str(e).lower() for e in exts)


def valid_gcode_exts_probe(file_manager: Any) -> ProbeAnswer:
    """Generic: the file manager module's VALID_GCODE_EXTS, which decides what
    Moonraker scans, lists as G-code and lets Klipper start."""
    exts = getattr(_file_manager_module(file_manager), "VALID_GCODE_EXTS", None)
    if exts is None:
        return None
    return ".3mf" in exts


def qidi_3mf_probe(file_manager: Any) -> ProbeAnswer:
    """QIDI: its Moonraker fork finishes a .3mf upload with
    FileManager._finish_3mf_upload and pulls plate thumbnails with
    MetadataStorage.parse_3mf_thumbnail (Q2 and Max 4 firmware)."""
    if hasattr(file_manager, "_finish_3mf_upload"):
        return True
    storage = getattr(file_manager, "gcode_metadata", None)
    if storage is not None and hasattr(storage, "parse_3mf_thumbnail"):
        return True
    module = _file_manager_module(file_manager)
    storage_class = getattr(module, "MetadataStorage", None)
    if storage_class is not None and hasattr(storage_class, "parse_3mf_thumbnail"):
        return True
    return None


# Vendor probes first: a fork that recognisably prints .3mf wins over a
# generic reading that could not tell.
NATIVE_FORMAT_PROBES = (qidi_3mf_probe, valid_gcode_exts_probe)


def prints_3mf_natively(
    file_manager: Any, probes: Iterable[NativeFormatProbe] = NATIVE_FORMAT_PROBES
) -> ProbeAnswer:
    """Whether this Moonraker prints .3mf itself: True if any probe says so,
    False if one says it does not and none says it does, None if none can
    tell."""
    answers = [probe(file_manager) for probe in probes]
    if any(a is True for a in answers):
        return True
    if any(a is False for a in answers):
        return False
    return None
