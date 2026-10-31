#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""List the source files under src/, include/ and tests/ a link is built from.

    test_link_sources.py OBJ_DIR < objects

Reads object paths (whitespace-separated) on stdin and prints, one per line and
sorted, every repository path under src/, include/ or tests/ those objects
depend on: each object's own .d file (its source and every header it
included), or, for an object with no .d yet, the source its path names.

scripts/tree_stamp.sh --paths hashes only these files, so the helix-tests
stale-binary stamp moves only when something the binary is compiled from
changes: an edit to src/main.cpp, which the test link leaves out, is not one.
"""

from __future__ import annotations

import os
import sys

ROOTS = ("src/", "include/", "tests/")
SOURCE_EXTS = (".cpp", ".c", ".mm", ".cc")


def deps_of(dep_file: str) -> list[str]:
    """Every prerequisite a make-style .d file names, targets dropped."""
    with open(dep_file, encoding="utf-8", errors="replace") as fh:
        text = fh.read().replace("\\\n", " ")
    out = []
    for line in text.splitlines():
        _, sep, rest = line.partition(": ")
        if not sep:
            _, sep, rest = line.partition(":")
        if sep:
            out.extend(rest.split())
    return out


def guessed_sources(obj: str, obj_dir: str) -> list[str]:
    """The source an object with no .d is compiled from, by the tree's layout:
    <obj_dir>/X.o comes from src/X.* and <obj_dir>/tests/X.o from tests/X.* or
    tests/unit/X.*."""
    rel = os.path.relpath(obj, obj_dir)
    stem = os.path.splitext(rel)[0]
    candidates = [os.path.join("src", stem), stem]
    if stem.startswith("tests/"):
        candidates.append(os.path.join("tests/unit", stem[len("tests/") :]))
    found = []
    for c in candidates:
        for ext in SOURCE_EXTS:
            if os.path.isfile(c + ext):
                found.append(c + ext)
    return found


def link_sources(objects: list[str], obj_dir: str, top: str) -> list[str]:
    paths: set[str] = set()
    for obj in objects:
        if not obj.endswith(".o"):
            continue
        dep_file = os.path.splitext(obj)[0] + ".d"
        names = deps_of(dep_file) if os.path.isfile(dep_file) else guessed_sources(obj, obj_dir)
        for name in names:
            rel = os.path.relpath(name, top) if os.path.isabs(name) else os.path.normpath(name)
            if rel.startswith(ROOTS):
                paths.add(rel)
    return sorted(paths)


def main(argv: list[str]) -> int:
    if len(argv) != 1:
        print(__doc__.strip().splitlines()[2].strip(), file=sys.stderr)
        return 2
    for path in link_sources(sys.stdin.read().split(), argv[0], os.getcwd()):
        print(path)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
