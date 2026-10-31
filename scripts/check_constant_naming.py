#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Fail on a NEW UPPER_SNAKE_CASE constant declaration in added lines.

Named C++ constants are kCamelCase; UPPER_SNAKE_CASE is for preprocessor macros
only, because a macro ignores scope and silently rewrites any constant that
shares its name. scripts/rename_constants.py converted the tree and owns the
rules this gate applies (what counts as a declaration, what counts as
UPPER_SNAKE), so the two cannot disagree.

Only added lines are read, and a name is new only when no removed line of the
same file declared it: a line that changed for another reason (a rename
elsewhere on it, a reformat) keeps the name it already had. Names the rename
skipped (a #define clash, an enumerator, data use) stay legal where they are.

    scripts/check_constant_naming.py --staged        # the index against HEAD
    scripts/check_constant_naming.py --base REV      # REV...HEAD, committed work
    scripts/check_constant_naming.py --base auto     # nearest fork point (diff_base.py)

Opt out on the declaration or the line above it: `// CONST_NAME_OK: <reason>`.
"""

import argparse
import re
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from rename_constants import (  # noqa: E402
    C_EXT,
    classify,
    code_only,
    find_const_decls,
    is_generated,
    to_k_camel,
)

ESCAPE = "CONST_NAME_OK:"
HUNK_RE = re.compile(r"^@@ -\d+(?:,\d+)? \+(\d+)(?:,(\d+))? @@")


def git(root, *args):
    r = subprocess.run(["git", *args], cwd=root, stdout=subprocess.PIPE,
                       stderr=subprocess.DEVNULL)
    return r.stdout.decode("utf-8", "replace") if r.returncode == 0 else ""


def parse_diff(text):
    """{path: (added [(lineno, text)], removed [text])} from a -U0 diff."""
    files, path, lineno = {}, None, 0
    for line in text.splitlines():
        if line.startswith("+++ "):
            path = line[6:] if line.startswith("+++ b/") else None
            if path is not None:
                files.setdefault(path, ([], []))
        elif line.startswith("--- "):
            continue
        elif line.startswith("@@"):
            m = HUNK_RE.match(line)
            lineno = int(m.group(1)) if m else 0
        elif path is None:
            continue
        elif line.startswith("+"):
            files[path][0].append((lineno, line[1:]))
            lineno += 1
        elif line.startswith("-"):
            files[path][1].append(line[1:])
    return files


def declared(line):
    """Names a single source line declares as UPPER_SNAKE constants.

    A line read alone cannot see an enclosing block comment, so one that opens
    like comment text is not read as code.
    """
    if line.lstrip().startswith(("*", "/*", "//")):
        return set()
    return {name for name, _ in find_const_decls(code_only(line))}


def findings(diff_text, read_new):
    """[(path, lineno, name, line)] for each new UPPER_SNAKE declaration.

    read_new(path) returns the file's new content, for the line above an
    added line and the generated-file header.
    """
    out = []
    for path, (added, removed) in sorted(parse_diff(diff_text).items()):
        if classify(path) != "cxx" or Path(path).suffix in C_EXT:
            continue
        old_names = set()
        for line in removed:
            old_names |= declared(line)
        new_lines = None
        for lineno, line in added:
            names = declared(line) - old_names
            if not names or ESCAPE in line:
                continue
            if new_lines is None:
                content = read_new(path)
                if is_generated(content):
                    break
                new_lines = content.splitlines()
            above = new_lines[lineno - 2] if 2 <= lineno <= len(new_lines) + 1 else ""
            if ESCAPE in above:
                continue
            for name in sorted(names):
                out.append((path, lineno, name, line.strip()))
    return out


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    mode = ap.add_mutually_exclusive_group(required=True)
    mode.add_argument("--staged", action="store_true", help="check the index against HEAD")
    mode.add_argument("--base", metavar="REV", help="check REV...HEAD; 'auto' = diff_base.py")
    ap.add_argument("--root", default=".")
    args = ap.parse_args(argv)
    root = Path(git(Path(args.root), "rev-parse", "--show-toplevel").strip() or args.root)

    if args.staged:
        diff = git(root, "diff", "--cached", "-U0", "--no-ext-diff", "--diff-filter=AM")
        def read_new(p):
            return git(root, "show", f":{p}")
        scope = "staged"
    else:
        base = args.base
        if base == "auto":
            from diff_base import resolve_base  # noqa: E402
            base, _ = resolve_base(root)
        if base == "HEAD":
            print("✅ Constant naming: no committed work to check")
            return 0
        diff = git(root, "diff", "-U0", "--no-ext-diff", "--diff-filter=AM", f"{base}...HEAD")
        def read_new(p):
            return git(root, "show", f"HEAD:{p}")
        scope = f"{base[:12]}...HEAD"

    hits = findings(diff, read_new)
    if not hits:
        print(f"✅ Constant naming: no new UPPER_SNAKE constants ({scope})")
        return 0
    for path, lineno, name, line in hits:
        print(f"{path}:{lineno}: {name} -> {to_k_camel(name)}: {line}")
    print(f"❌ {len(hits)} new UPPER_SNAKE constant(s): name constants kCamelCase; "
          "UPPER_SNAKE is for macros.")
    print(f"   A deliberate one takes `// {ESCAPE} <reason>` on the line or the one above.")
    return 1


if __name__ == "__main__":
    sys.exit(main())
