#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Flag an lv_xml_create() whose result is hidden right after it is built.

The tree is visible while lv_xml_create() builds it, so a widget that runs a
layout pass during the build (a dropdown with a selection does) queues the
root's whole area for redraw even though the caller hides it on return. That is
a wasted full render on every open on slow displays. Build it with
helix::ui::create_xml_hidden() (include/ui_panel_common.h) instead.

A create that is meant to be visible-then-hidden (a pooled list row built under
its list) takes `// XML_HIDDEN_OK: <reason>` on the create or the hide line.

Usage: check_xml_create_then_hide.py [FILE...]   (default: src/ and plugins/)
"""

import re
import subprocess
import sys

WINDOW = 40  # lines after the create in which a hide counts as "right after"
HATCH = "XML_HIDDEN_OK"

ASSIGN_RE = re.compile(
    r"(?P<var>[A-Za-z_][\w.\->]*)\s*=\s*"
    r"(?:static_cast<\s*lv_obj_t\s*\*\s*>\s*\(|\(\s*lv_obj_t\s*\*\s*\)\s*)?"
    r"\s*(?:static_cast<\s*lv_obj_t\s*\*\s*>\s*\()?\s*lv_xml_create\s*\(",
    re.S,
)


def default_files():
    out = subprocess.check_output(
        ["git", "ls-files", "--cached", "--others", "--exclude-standard", "src", "plugins"],
        text=True,
    )
    return [f for f in out.split() if f.endswith((".cpp", ".h"))]


def offenders(path):
    try:
        text = open(path, encoding="utf-8", errors="replace").read()
    except OSError:
        return []
    lines = text.split("\n")
    found = []
    for m in ASSIGN_RE.finditer(text):
        var = m.group("var")
        start = text.count("\n", 0, m.start())
        end = text.count("\n", 0, m.end())
        if any(HATCH in lines[i] for i in range(start, end + 1)):
            continue
        hide = re.compile(r"lv_obj_add_flag\(\s*" + re.escape(var) + r"\s*,\s*LV_OBJ_FLAG_HIDDEN\b")
        for i in range(end, min(end + WINDOW, len(lines))):
            if hide.search(lines[i]):
                if HATCH not in lines[i]:
                    found.append(f"{path}:{start + 1}: {var} built visible, hidden at line {i + 1}")
                break
    return found


def main(argv):
    files = argv[1:] or default_files()
    bad = [o for f in files for o in offenders(f)]
    if not bad:
        print("✅ XML create-then-hide: none")
        return 0
    print("❌ lv_xml_create() result hidden right after the build (a wasted full render):")
    for o in bad:
        print(f"   {o}")
    print("   Use helix::ui::create_xml_hidden() (include/ui_panel_common.h).")
    print(f"   Meant to be built visible? // {HATCH}: <reason>")
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
