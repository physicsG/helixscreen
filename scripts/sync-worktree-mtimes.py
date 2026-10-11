#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Adopt the main tree's mtimes for worktree files with byte-identical content.

Why this exists
---------------
`git worktree add` writes every file fresh, so the whole checkout carries the
checkout-time mtime. That is newer than the build artifacts cloned from the main
tree, so make rebuilds essentially everything:

  - the .d files list include/*.h (lvgl_pch.h and lv_conf.h among them) as
    prerequisites of each object -> one fresh header invalidates every object
    that includes it, and the force-included lvgl_pch.h reaches all ~1970.

So this walks the whole checkout.

Why it is not "back-dating mtimes"
----------------------------------
A file's mtime is only changed when its content is *byte-identical* to the main
tree's file at the same path, and the value adopted is that same main-tree
file's mtime -- never "now minus something", never an invented timestamp.

The resulting invariant is: for every file whose content matches, the
(source mtime, object mtime) ordering in the worktree is exactly the ordering in
the main tree. make therefore reaches the same up-to-date decision in the
worktree that it reaches in the main tree, for a source that is the same bytes,
against an object cloned from that same main tree. Any file that differs -- a
branch with real changes, or any later edit -- keeps its fresh checkout mtime
and rebuilds normally.

The one thing this inherits rather than fixes: if the main tree's own build is
stale, the worktree faithfully reproduces that staleness. The object clone
already had that property; this does not make it worse.
"""

from __future__ import annotations

import argparse
import os
import stat as statmod
import subprocess
import sys

CHUNK = 1 << 20


def same_content(a: str, b: str, size: int) -> bool:
    """Byte-compare two regular files already known to have equal size."""
    if size == 0:
        return True
    with open(a, "rb") as fa, open(b, "rb") as fb:
        while True:
            ba = fa.read(CHUNK)
            bb = fb.read(CHUNK)
            if ba != bb:
                return False
            if not ba:
                return True


def tracked_paths(worktree: str) -> list[str]:
    out = subprocess.run(
        ["git", "-C", worktree, "ls-files", "-z"],
        check=True,
        stdout=subprocess.PIPE,
    ).stdout
    return [p.decode("utf-8", "surrogateescape") for p in out.split(b"\0") if p]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--main", required=True, help="main tree root")
    ap.add_argument("--worktree", required=True, help="worktree root to adjust")
    ap.add_argument("--quiet", action="store_true")
    args = ap.parse_args()

    main_tree = os.path.realpath(args.main)
    worktree = os.path.realpath(args.worktree)

    if main_tree == worktree:
        print("sync-worktree-mtimes: main tree and worktree are the same path; nothing to do")
        return 0
    if not os.path.isdir(os.path.join(main_tree, ".git")) and not os.path.isfile(
        os.path.join(main_tree, ".git")
    ):
        print(f"sync-worktree-mtimes: {main_tree} is not a git tree", file=sys.stderr)
        return 1

    synced = 0
    differed = 0
    skipped = 0
    already = 0

    for rel in tracked_paths(worktree):
        wt = os.path.join(worktree, rel)
        mt = os.path.join(main_tree, rel)
        try:
            # lstat: never follow symlinks. lib/<submodule> is a symlink into the
            # main tree in a configured worktree, and the gitlink entries are
            # directories -- neither is ours to touch.
            wst = os.lstat(wt)
            mst = os.lstat(mt)
        except OSError:
            skipped += 1
            continue

        if not statmod.S_ISREG(wst.st_mode) or not statmod.S_ISREG(mst.st_mode):
            skipped += 1
            continue
        if wst.st_size != mst.st_size:
            differed += 1
            continue
        if wst.st_mtime_ns == mst.st_mtime_ns:
            already += 1
            continue
        try:
            if not same_content(wt, mt, wst.st_size):
                differed += 1
                continue
            os.utime(wt, ns=(mst.st_atime_ns, mst.st_mtime_ns))
            synced += 1
        except OSError:
            skipped += 1

    if not args.quiet:
        print(
            f"  mtime sync: {synced} adopted, {already} already matching, "
            f"{differed} differ (will rebuild), {skipped} skipped"
        )
    return 0


if __name__ == "__main__":
    sys.exit(main())
