#!/usr/bin/env python3
# Copyright (C) 2025-2026 356C LLC
# SPDX-License-Identifier: GPL-3.0-or-later
"""What a `--staged-only` gate must read: the INDEX, not the working tree.

A `--staged-only` gate exists so the pre-commit hook checks what the commit
will actually contain. Picking the file list from `git diff --cached
--name-only` and then reading each path with `open()` / `Path.read_text()`
gets the SET right and the CONTENT wrong: that call reads whatever is on disk
right now, which is the staged blob only until something touches the working
tree again. Stage a violation, then revert the working copy to something
clean - the file list still names the file, but every gate built this way
reads the clean copy and reports nothing. The bad blob commits.

This module reads the staged blob instead, via one `git cat-file --batch`
process for every path a gate cares about, so nine gates stop hand-rolling
nine copies of the same subprocess dance.

    from staged_content import staged_files

    for path, text in staged_files(suffixes=('.bats',)):
        ...

`staged_files()` covers the common case: the set of files worth checking IS
the staged diff, and each one is a per-file rule (a duplicate name, a bare
`[[ ]]`, an unguarded subscript) that never needs to see a file that did not
change. That is every gate in this module's first set of callers.

A gate whose count is a WHOLE-TREE ratchet (check_hardcoded_pixels.py,
check_panel_widget_scrollable.py) needs a different shape: every file in the
tree the commit will produce, not just the changed ones, so an unrelated
file's pre-existing violations still count toward the baseline. Those two
build that tree with `git write-tree` + `ls-tree`, then read it through
`catfile_batch()` below - the file SET is a different question from this
module's `staged_paths()`, so it is not duplicated here, but the READ
primitive is shared.
"""

from __future__ import annotations

import subprocess
from pathlib import Path
from typing import Iterable, Iterator


def repo_root() -> Path:
    out = subprocess.run(
        ['git', 'rev-parse', '--show-toplevel'],
        capture_output=True, text=True, check=False,
    ).stdout.strip()
    return Path(out) if out else Path.cwd()


def staged_paths(root: Path | None = None, suffixes: tuple[str, ...] | None = None,
                  diff_filter: str = 'ACMRT') -> list[str]:
    """Relative paths staged for commit, as `git diff --cached` reports them.

    `-z` NUL-separates the output and disables path C-quoting, so a staged
    name holding a non-ASCII byte, a literal `"`, or a newline still comes
    back as the real path instead of a quoted-and-escaped string that no
    later `:path` lookup will resolve.

    `diff_filter` defaults to Added/Copied/Modified/Renamed/Type-changed.
    `git diff --cached --name-only` reports a rename's DESTINATION path only
    (verified against `git diff --cached --name-status`), and that path
    already resolves in the index like any other entry, so admitting R and T
    costs nothing extra downstream - the alternative, the old ACM-only
    default, silently skipped every renamed-with-an-edit file. A path staged
    for deletion still has no index entry to read; callers that want to see
    those too pass a filter that includes D and are expected to cope with
    `read_index_blobs` skipping a path that vanished.
    """
    root = root or repo_root()
    out = subprocess.run(
        ['git', '-C', str(root), 'diff', '--cached', '--name-only', '-z',
         f'--diff-filter={diff_filter}'],
        capture_output=True, text=True, check=False,
    ).stdout
    paths = [p for p in out.split('\0') if p]
    if suffixes:
        paths = [p for p in paths if p.endswith(suffixes)]
    return paths


def catfile_batch(items: Iterable[tuple[str, str]],
                   root: Path | None = None) -> Iterator[tuple[str, str]]:
    """Yield (label, text) for each (label, revision) pair, via ONE `git
    cat-file --batch` process.

    `revision` is any rev-spec `cat-file` accepts - `:path` for the index
    (stage 0), `<tree>:path` for an arbitrary tree object built with
    `git write-tree`. Every gate that needs to stream more than one blob out
    of git shares this one process-management primitive, whatever file SET
    it chose to read.

    Requests are written one at a time, each followed by a `flush()` and an
    immediate read of that request's response, before the next request is
    written. Writing every request first and reading afterward deadlocks
    once the path list is large enough: git's stdout fills the OS pipe
    buffer (a few dozen KB) before this process finishes writing, git blocks
    on that full pipe, and this process's own stdin write then blocks too -
    neither side is reading the other free, forever. A label with no blob at
    its revision (deleted, a gitlink/submodule, or a bad spec) reports
    "missing" and is skipped, never yielded with stale content.

    A revision containing a literal newline (a path with an embedded `\\n`,
    which `-z` delivers as a real byte rather than escaping it) never enters
    that stream: `cat-file --batch`'s protocol is one revision per line, so
    the embedded newline would read as two requests to git and desync every
    response after it - not just this one path, but the next real entry
    behind it too. Read separately instead, with the revision passed as an
    argv to a one-off `git cat-file blob`, which carries the byte safely.
    """
    items = list(items)
    if not items:
        return
    root = root or repo_root()
    proc: subprocess.Popen | None = None
    try:
        for label, revision in items:
            if '\n' in revision:
                out = subprocess.run(
                    ['git', '-C', str(root), 'cat-file', 'blob', revision],
                    capture_output=True, check=False,
                )
                if out.returncode == 0:
                    yield (label, out.stdout.decode('utf-8', 'ignore'))
                continue

            if proc is None:
                proc = subprocess.Popen(
                    ['git', '-C', str(root), 'cat-file', '--batch'],
                    stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                )
            assert proc.stdin is not None and proc.stdout is not None
            proc.stdin.write(f'{revision}\n'.encode())
            proc.stdin.flush()
            header = proc.stdout.readline().decode('utf-8', 'replace').split()
            # "<sha> blob <size>" - anything else (missing, a non-blob type)
            # has no bytes to read and nothing following it to skip.
            if len(header) < 3 or header[1] != 'blob':
                continue
            size = int(header[2])
            content = proc.stdout.read(size)
            proc.stdout.read(1)  # trailing newline after each blob
            yield (label, content.decode('utf-8', 'ignore'))
    finally:
        if proc is not None:
            if proc.stdin is not None and not proc.stdin.closed:
                proc.stdin.close()
            proc.wait()


def read_index_blobs(paths: Iterable[str], root: Path | None = None) -> Iterator[tuple[str, str]]:
    """Yield (path, text) reading each path's STAGED content (index stage 0).

    The pre-commit hook runs on every commit across many sessions, so this
    streams every blob through one `catfile_batch()` process rather than
    spawning one per file.
    """
    root = root or repo_root()
    yield from catfile_batch(((p, f':{p}') for p in paths), root)


def staged_files(root: Path | None = None, suffixes: tuple[str, ...] | None = None,
                  diff_filter: str = 'ACMRT') -> Iterator[tuple[str, str]]:
    """Yield (path, text) for every staged file, content from the index.

    The set (`staged_paths`) and the content (`read_index_blobs`) a
    `--staged-only` gate wants, in one call.
    """
    root = root or repo_root()
    yield from read_index_blobs(staged_paths(root, suffixes, diff_filter), root)
