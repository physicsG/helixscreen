#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Turn a Catch2 XML test listing into a filter file --input-file can replay.

Reads `helix-tests --list-tests --reporter xml` on stdin, writes one test name
per line on stdout, escaped so Catch2 parses each line as a single test spec.

Catch2 splits a spec on commas and reads [] as tags and * as a wildcard, so a
name carrying any of those has to arrive backslash-escaped; quoting the name
does not work. The human `--list-tests` output cannot be used for this at all:
it indents tags under each name and wraps long names across lines, so a line
count there is not a test count.

`--failed-in LOG` keeps only the cases whose block in LOG (the console
reporter's output for the same run) holds a `FAILED` line. The console's
header wraps a long name, so a case matches by its name with whitespace
ignored, or by the case location printed under the header.
"""

from __future__ import annotations

import argparse
import re
import sys
import xml.etree.ElementTree as ET

# Backslash first, or it would escape the escapes added after it.
_SPECIAL = "\\,[]*"


def escape(name: str) -> str:
    out = []
    for ch in name:
        if ch in _SPECIAL:
            out.append("\\")
        out.append(ch)
    return "".join(out)


_RULE = re.compile(r"^-{20,}$")
_LOCATION = re.compile(r"^(.+):(\d+)$")
_FAILED = re.compile(r":\d+: FAILED")


def _compact(name: str) -> str:
    """A name with its whitespace removed: the console wraps a long name at
    any column, so only the characters survive the round trip."""
    return "".join(name.split())


def failed_cases(log_text: str) -> tuple[set[str], set[tuple[str, str]]]:
    """The test cases the console log reports as failed, as compacted names
    and as (file, line) locations.

    A failure block opens with a header: a dashed rule, the case name (wrapped
    onto unindented lines when long), its sections indented beneath, a second
    rule, then a `file:line` - the case's, or the innermost section's. Every
    `FAILED` line after that belongs to it until the next header.
    """
    names: set[str] = set()
    places: set[tuple[str, str]] = set()
    current_name = None
    current_place = None
    header: list[str] | None = None
    expect_location = False
    for line in log_text.splitlines():
        line = line.rstrip("\r")
        if _RULE.match(line):
            if header is None:
                header = []
            else:
                current_name = _compact("".join(l for l in header if not l.startswith(" ")))
                header = None
                expect_location = True
            continue
        if header is not None:
            header.append(line)
            continue
        if expect_location:
            expect_location = False
            m = _LOCATION.match(line.strip())
            current_place = (m.group(1), m.group(2)) if m else None
            continue
        if current_name is not None and _FAILED.search(line):
            names.add(current_name)
            if current_place:
                places.add(current_place)
    return names, places


def _same_place(listed: tuple[str, str], logged: tuple[str, str]) -> bool:
    if listed[1] != logged[1]:
        return False
    a, b = listed[0], logged[0]
    return a == b or a.endswith("/" + b) or b.endswith("/" + a)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--failed-in", metavar="LOG", help="keep only cases LOG reports as failed")
    args = parser.parse_args(argv if argv is not None else [])
    raw = sys.stdin.read()
    if not raw.strip():
        return 0
    try:
        root = ET.fromstring(raw)
    except ET.ParseError as exc:
        print(f"catch2_shard_tests: unparseable listing: {exc}", file=sys.stderr)
        return 1
    failed = None
    if args.failed_in:
        try:
            with open(args.failed_in, encoding="utf-8", errors="replace") as fh:
                failed = failed_cases(fh.read())
        except OSError as exc:
            print(f"catch2_shard_tests: {exc}", file=sys.stderr)
            return 1
    for case in root.iter("TestCase"):
        name = case.findtext("Name")
        if not name:
            continue
        if failed is not None:
            names, places = failed
            place = (case.findtext("SourceInfo/File") or "", case.findtext("SourceInfo/Line") or "")
            if _compact(name) not in names and not any(_same_place(place, f) for f in places):
                continue
        print(escape(name))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
