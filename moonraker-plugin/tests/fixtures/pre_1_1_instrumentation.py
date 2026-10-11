# SPDX-License-Identifier: GPL-3.0-or-later
"""
A pre-1.1 HelixScreen instruments and de-instruments a PRINT_START macro this
way: it locates the macro's gcode: body across the config tree, injects or
removes HELIX_TRACKING marker blocks in that body text, and splices the
result back into the config file that defines it, with a timestamped backup.

This is that logic, taken as-is and trimmed of the Moonraker component
plumbing (the HTTP handlers, KlippyApis, logging) around it, so
strip_phase_tracking.py's output can be checked against it directly as an
equivalence oracle - without importing a live plugin or depending on git
history.

Two writer generations are represented:
- `enable`/`disable` (the "v2" writer): matches TRACKING_MARKER_BEGIN/END,
  finds the macro's body with an anchored, case-insensitive section-header
  search, and re-indents an injected block to the body's own indentation.
- `enable_legacy`/`disable_legacy` (the pre-#1268 writer): the same marker
  format, but an unanchored header search, an unsorted file scan, a fixed
  four-space indent applied to the whole body on every write, and a body
  locator that stops at the first blank line - so a macro with a blank line
  in its body keeps an un-instrumented, duplicated tail after either write.
"""

import re
import shutil
import time
from pathlib import Path
from typing import Optional, Tuple

TRACKING_MARKER_BEGIN = "# <<< HELIX_TRACKING v2 >>>"
TRACKING_MARKER_END = "# <<< /HELIX_TRACKING >>>"

PHASE_PATTERNS = [
    (r"\bG28\b", "HELIX_PHASE_HOMING"),
    (r"\bQUAD_GANTRY_LEVEL\b", "HELIX_PHASE_QGL"),
    (r"\bZ_TILT_ADJUST\b", "HELIX_PHASE_Z_TILT"),
    (r"\bBED_MESH_CALIBRATE\b", "HELIX_PHASE_BED_MESH"),
    (r"\b(CLEAN|WIPE)_NOZZLE\b", "HELIX_PHASE_CLEANING"),
    (r"\b\w*PURGE\w*\b", "HELIX_PHASE_PURGING"),
    (r"\bM109\b", "HELIX_PHASE_HEATING_NOZZLE"),
    (r"\bM190\b", "HELIX_PHASE_HEATING_BED"),
]

MACRO_NAMES = ["PRINT_START", "START_PRINT", "_PRINT_START"]


# ============================================================================
# v2 writer (matches strip_phase_tracking.py's marker format)
# ============================================================================


def _locate_macro_body(content: str, macro_name: str) -> Optional[Tuple[int, int]]:
    """Locate a macro's gcode: body within one config file's text.

    Returns (body_start, body_end) character offsets into content. A body
    runs until a line that is non-blank and starts in column 0; blank lines
    belong to the body.
    """
    match = re.search(
        rf"^\[gcode_macro\s+{re.escape(macro_name)}\]",
        content,
        re.IGNORECASE | re.MULTILINE,
    )
    if not match:
        return None

    pos = match.end()
    body_start: Optional[int] = None
    body_end: Optional[int] = None

    while pos < len(content):
        newline = content.find("\n", pos)
        line_end = len(content) if newline == -1 else newline
        next_pos = len(content) if newline == -1 else newline + 1
        line = content[pos:line_end]

        if line.startswith("["):
            break

        if body_start is None:
            if line.strip().startswith("gcode:"):
                after = line.split("gcode:", 1)[1]
                if after.strip():
                    body_start = line_end - len(after)
                    body_end = line_end
                else:
                    body_start = next_pos
                    body_end = next_pos
            pos = next_pos
            continue

        if line.strip() and not line[0].isspace():
            break

        body_end = line_end
        pos = next_pos

    if body_start is None or body_end is None:
        return None
    return (body_start, body_end)


def _find_macro(config_dir: Path, macro_name: str) -> Optional[Tuple[Path, str, int, int]]:
    """Find a macro's gcode: body across the config tree, sorted for a
    deterministic file when the name is defined more than once."""
    for cfg_file in sorted(config_dir.glob("**/*.cfg")):
        try:
            content = cfg_file.read_text()
        except Exception:
            continue

        located = _locate_macro_body(content, macro_name)
        if located is None:
            continue

        body_start, body_end = located
        if not content[body_start:body_end].strip():
            continue

        return (cfg_file, content, body_start, body_end)

    return None


def _get_print_start_macro(config_dir: Path) -> Tuple[str, Optional[str]]:
    for name in MACRO_NAMES:
        found = _find_macro(config_dir, name)
        if found is not None:
            _cfg_file, content, body_start, body_end = found
            return (name, content[body_start:body_end])
    return (MACRO_NAMES[0], None)


def _body_indent(body: str) -> str:
    for line in body.split("\n"):
        if line.strip() and line[0].isspace():
            return line[: len(line) - len(line.lstrip())]
    return "    "


def _reindent_body(gcode: str, indent: str) -> str:
    lines = []
    for line in gcode.split("\n"):
        if not line.strip():
            lines.append("")
        elif line[0].isspace():
            lines.append(line)
        else:
            lines.append(indent + line)
    return "\n".join(lines)


def _defined_macro_names(config_dir: Path) -> set:
    names = set()
    for cfg_file in sorted(config_dir.glob("**/*.cfg")):
        try:
            content = cfg_file.read_text()
        except Exception:
            continue
        for match in re.finditer(r"^\[gcode_macro\s+([^\]]+)\]", content, re.MULTILINE):
            names.add(match.group(1).strip().upper())
    return names


def _injected_macro_names(instrumented: str) -> set:
    names = set()
    in_block = False
    for line in instrumented.split("\n"):
        if TRACKING_MARKER_BEGIN in line:
            in_block = True
            continue
        if TRACKING_MARKER_END in line:
            in_block = False
            continue
        if in_block and line.strip():
            names.add(line.strip().split()[0].upper())
    return names


def _instrument_gcode(gcode: str) -> str:
    lines = gcode.split("\n")
    result = []

    for line in lines:
        result.append(line)
        line_upper = line.upper().strip()
        if not line_upper or line_upper.startswith("#"):
            continue
        for pattern, macro_name in PHASE_PATTERNS:
            if re.search(pattern, line_upper, re.IGNORECASE):
                result.append(TRACKING_MARKER_BEGIN)
                result.append(macro_name)
                result.append(TRACKING_MARKER_END)
                break

    result.append(TRACKING_MARKER_BEGIN)
    result.append("HELIX_READY")
    result.append(TRACKING_MARKER_END)

    return "\n".join(result)


def _strip_instrumentation(gcode: str) -> str:
    lines = gcode.split("\n")
    result = []
    skip = False

    for line in lines:
        if TRACKING_MARKER_BEGIN in line:
            skip = True
            continue
        if TRACKING_MARKER_END in line:
            skip = False
            continue
        if not skip:
            result.append(line)

    return "\n".join(result)


def _update_macro(config_dir: Path, macro_name: str, gcode: str) -> bool:
    found = _find_macro(config_dir, macro_name)
    if found is None:
        return False

    cfg_file, content, body_start, body_end = found
    new_body = _reindent_body(gcode, _body_indent(content[body_start:body_end]))
    new_content = content[:body_start] + new_body + content[body_end:]

    if new_content == content:
        return True

    try:
        backup_path = cfg_file.with_suffix(f".bak.{int(time.time())}")
        shutil.copy(cfg_file, backup_path)
        cfg_file.write_text(new_content)
        return True
    except Exception:
        return False


def enable(config_dir: Path) -> dict:
    """Instrument config_dir's PRINT_START (or START_PRINT/_PRINT_START)."""
    macro_name, gcode = _get_print_start_macro(config_dir)
    if not gcode:
        return {"success": False, "error": "PRINT_START macro not found", "macro_name": macro_name}

    if TRACKING_MARKER_BEGIN in gcode:
        return {"success": True, "already_instrumented": True, "macro_name": macro_name}

    instrumented = _instrument_gcode(gcode)

    missing = sorted(_injected_macro_names(instrumented) - _defined_macro_names(config_dir))
    if missing:
        return {
            "success": False,
            "error": "helix_macros.cfg is not installed - missing macros: " + ", ".join(missing),
            "macro_name": macro_name,
            "missing_macros": missing,
        }

    success = _update_macro(config_dir, macro_name, instrumented)
    return {"success": success, "macro_name": macro_name, "instrumented": success}


def disable(config_dir: Path) -> dict:
    """Strip config_dir's PRINT_START instrumentation back out."""
    macro_name, gcode = _get_print_start_macro(config_dir)
    if not gcode:
        return {"success": False, "error": "PRINT_START macro not found", "macro_name": macro_name}

    if TRACKING_MARKER_BEGIN not in gcode:
        return {"success": True, "was_instrumented": False, "macro_name": macro_name}

    stripped = _strip_instrumentation(gcode)
    success = _update_macro(config_dir, macro_name, stripped)
    return {"success": success, "macro_name": macro_name, "was_instrumented": True}


# ============================================================================
# Legacy (pre-#1268) writer
# ============================================================================
#
# Same marker format, but the read path and the write path locate the body
# two different ways. Reading (for instrumenting) collects every indented
# line including blank ones. Writing replaces only what
# `r"gcode:\s*\n((?:[ \t]+.*\n)*)"` captures, which stops at the first line
# with no leading whitespace - a blank line included. A macro with a blank
# line partway through its body therefore gets a second, un-instrumented
# copy of everything after that line: the write only ever touches the part
# before it.


def _read_macro_body_legacy(config_dir: Path, macro_name: str) -> Optional[str]:
    for cfg_file in config_dir.glob("**/*.cfg"):
        try:
            content = cfg_file.read_text()
        except Exception:
            continue

        match = re.search(rf"\[gcode_macro\s+{macro_name}\]", content, re.IGNORECASE)
        if not match:
            continue

        lines = []
        in_gcode = False
        for line in content[match.end():].split("\n"):
            if line.startswith("[") and not line.startswith("[gcode_macro"):
                break
            if re.match(r"^\[gcode_macro", line, re.IGNORECASE):
                break
            if line.strip().startswith("gcode:"):
                in_gcode = True
                after_gcode = line.split("gcode:", 1)[1].strip()
                if after_gcode:
                    lines.append(after_gcode)
                continue
            if in_gcode:
                if line and not line[0].isspace() and line.strip():
                    break
                lines.append(line)

        if lines:
            return "\n".join(lines)

    return None


def _get_print_start_macro_legacy(config_dir: Path) -> Tuple[str, Optional[str]]:
    for name in MACRO_NAMES:
        gcode = _read_macro_body_legacy(config_dir, name)
        if gcode:
            return (name, gcode)
    return (MACRO_NAMES[0], None)


def _update_macro_legacy(config_dir: Path, macro_name: str, gcode: str) -> bool:
    for cfg_file in config_dir.glob("**/*.cfg"):
        try:
            content = cfg_file.read_text()

            pattern = rf"\[gcode_macro\s+{re.escape(macro_name)}\]"
            match = re.search(pattern, content, re.IGNORECASE)
            if not match:
                continue

            backup_path = cfg_file.with_suffix(f".bak.{int(time.time())}")
            shutil.copy(cfg_file, backup_path)

            section_start = match.start()
            section_end = len(content)
            next_section = re.search(r"\n\[", content[match.end():])
            if next_section:
                section_end = match.end() + next_section.start()
            section = content[section_start:section_end]

            gcode_match = re.search(r"gcode:\s*\n((?:[ \t]+.*\n)*)", section, re.MULTILINE)
            if not gcode_match:
                continue

            indent = "    "
            indented_gcode = "\n".join(
                indent + line if line.strip() else line for line in gcode.split("\n")
            )
            new_section = (
                section[: gcode_match.start(1)] + indented_gcode + "\n"
                + section[gcode_match.end(1):]
            )
            new_content = content[:section_start] + new_section + content[section_end:]
            cfg_file.write_text(new_content)
            return True
        except Exception:
            continue

    return False


def enable_legacy(config_dir: Path) -> dict:
    macro_name, gcode = _get_print_start_macro_legacy(config_dir)
    if not gcode:
        return {"success": False, "error": "PRINT_START macro not found", "macro_name": macro_name}
    if TRACKING_MARKER_BEGIN in gcode:
        return {"success": True, "already_instrumented": True, "macro_name": macro_name}
    instrumented = _instrument_gcode(gcode)
    success = _update_macro_legacy(config_dir, macro_name, instrumented)
    return {"success": success, "macro_name": macro_name, "instrumented": success}


def disable_legacy(config_dir: Path) -> dict:
    macro_name, gcode = _get_print_start_macro_legacy(config_dir)
    if not gcode:
        return {"success": False, "error": "PRINT_START macro not found", "macro_name": macro_name}
    if TRACKING_MARKER_BEGIN not in gcode:
        return {"success": True, "was_instrumented": False, "macro_name": macro_name}
    stripped = _strip_instrumentation(gcode)
    success = _update_macro_legacy(config_dir, macro_name, stripped)
    return {"success": success, "macro_name": macro_name, "was_instrumented": True}
