# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for scripts/staged_content.py - the git-index content reader shared
by every `--staged-only` lint gate.

Runs `git` against real throwaway repos under `tmp_path` rather than
mocking subprocess calls: the properties under test (a pipe deadlock, path
C-quoting, rename detection) are all behaviors of the real `git` binary that
a mock would have to reimplement to be worth anything.
"""

import json
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
SCRIPTS_DIR = REPO_ROOT / "scripts"
sys.path.insert(0, str(SCRIPTS_DIR))

import staged_content  # noqa: E402


def _init_repo(root: Path) -> None:
    subprocess.run(["git", "init", "-q"], cwd=root, check=True)
    subprocess.run(["git", "config", "user.email", "test@example.com"], cwd=root, check=True)
    subprocess.run(["git", "config", "user.name", "Test"], cwd=root, check=True)


def _write(root: Path, rel: str, content: str) -> None:
    p = root / rel
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text(content, encoding="utf-8")


def _commit(root: Path, message: str = "commit") -> None:
    subprocess.run(["git", "add", "-A"], cwd=root, check=True)
    subprocess.run(["git", "commit", "-q", "-m", message], cwd=root, check=True)


def _stage(root: Path) -> None:
    subprocess.run(["git", "add", "-A"], cwd=root, check=True)


# A driver run as a SEPARATE process so a real deadlock kills the subprocess
# under `subprocess.run`'s timeout instead of hanging the test runner itself.
_DRIVER = """
import json, sys
sys.path.insert(0, sys.argv[2])
import staged_content
root = sys.argv[1]
paths = staged_content.staged_paths(root=root)
blobs = dict(staged_content.read_index_blobs(paths, root=root))
print(json.dumps({
    "path_count": len(paths),
    "blob_count": len(blobs),
    "first": [paths[0], blobs.get(paths[0])] if paths else None,
    "last": [paths[-1], blobs.get(paths[-1])] if paths else None,
}))
"""


def test_read_index_blobs_completes_on_a_2000_file_staged_set(tmp_path):
    """Regression test for a pipe deadlock: writing every `:path` request to
    `git cat-file --batch`'s stdin before reading any response blocks once
    the request text crosses the OS pipe buffer (a few dozen KB) - git
    blocks on a full stdout while this process blocks on a full stdin, and
    neither side is reading the other free, forever. 2000 files under long
    nested paths (the shape of a trunk-swap merge or a mass rename/regen)
    crosses that buffer well before the file count alone would suggest a
    problem, so this asserts completion under a generous timeout with the
    right content at both ends of the set, not just "eventually returns".
    """
    _init_repo(tmp_path)
    count = 2000
    rels = []
    for i in range(count):
        rel = f"src/ui/panels/group_{i % 40:03d}/sub_{i % 7:02d}/ui_panel_widget_{i:05d}.cpp"
        _write(tmp_path, rel, f"void f{i}() {{ return {i}; }}\n")
        rels.append(rel)
    _stage(tmp_path)

    proc = subprocess.run(
        [sys.executable, "-c", _DRIVER, str(tmp_path), str(SCRIPTS_DIR)],
        capture_output=True, text=True, timeout=30,
    )
    assert proc.returncode == 0, f"stdout={proc.stdout!r} stderr={proc.stderr!r}"
    result = json.loads(proc.stdout)
    assert result["path_count"] == count
    assert result["blob_count"] == count
    # Order is whatever git's own index traversal produces, not asserted here;
    # what matters is that BOTH ends of a 2000-entry batch read back the exact
    # content their own name encodes, proving nothing desynced along the way.
    for rel, content in (result["first"], result["last"]):
        assert rel in rels
        i = int(rel.rsplit("_", 1)[1].split(".")[0])
        assert content == f"void f{i}() {{ return {i}; }}\n"


def test_staged_paths_handles_non_ascii_quoted_and_spaced_names(tmp_path):
    """`git diff --cached --name-only` (no `-z`) C-quotes a path holding a
    non-ASCII byte or a literal `"` - the string `"caf\\303\\251.txt"` comes
    back instead of the real name `café.txt`, and that mangled text then
    fails to resolve against the index at all. `-z` disables the quoting.
    """
    _init_repo(tmp_path)
    _write(tmp_path, "café.txt", "unicode name\n")
    _write(tmp_path, 'a "quoted" name.txt', "quoted name\n")
    _write(tmp_path, "a spaced name.txt", "spaced name\n")
    _stage(tmp_path)

    paths = staged_content.staged_paths(root=tmp_path)
    assert "café.txt" in paths
    assert 'a "quoted" name.txt' in paths
    assert "a spaced name.txt" in paths

    blobs = dict(staged_content.read_index_blobs(paths, root=tmp_path))
    assert blobs["café.txt"] == "unicode name\n"
    assert blobs['a "quoted" name.txt'] == "quoted name\n"
    assert blobs["a spaced name.txt"] == "spaced name\n"


def test_staged_paths_includes_a_renamed_and_edited_file(tmp_path):
    """The default `ACMRT` filter must see a rename that also carries an
    edit (`R` below 100% similarity) - the old `ACM`-only default silently
    dropped it, so `git diff --cached --name-only` reported nothing at all
    for a `git mv` plus a content change. `--name-only` reports only the
    DESTINATION path for a rename, and that path already resolves in the
    index like any other entry.
    """
    _init_repo(tmp_path)
    _write(tmp_path, "src/old.cpp", "\n".join(f"line{i}" for i in range(10)) + "\n")
    _commit(tmp_path)
    subprocess.run(["git", "mv", "src/old.cpp", "src/new.cpp"], cwd=tmp_path, check=True)
    _write(tmp_path, "src/new.cpp",
           "\n".join(f"line{i}" for i in range(9)) + "\nEXTRA\n")
    _stage(tmp_path)

    paths = staged_content.staged_paths(root=tmp_path, suffixes=(".cpp",))
    assert paths == ["src/new.cpp"]
    assert "src/old.cpp" not in paths

    blobs = dict(staged_content.read_index_blobs(paths, root=tmp_path))
    assert "EXTRA" in blobs["src/new.cpp"]


def test_catfile_batch_skips_a_missing_revision_without_desyncing(tmp_path):
    """A label with no blob at its revision (a path never staged) reports
    `missing` and must be skipped cleanly - a caller mixing real and bogus
    revisions in one batch must still get every real one back, in order.
    """
    _init_repo(tmp_path)
    _write(tmp_path, "a.txt", "a\n")
    _write(tmp_path, "b.txt", "b\n")
    _stage(tmp_path)

    items = [("a", ":a.txt"), ("missing", ":never-staged.txt"), ("b", ":b.txt")]
    result = dict(staged_content.catfile_batch(items, root=tmp_path))
    assert result == {"a": "a\n", "b": "b\n"}


def test_a_staged_path_with_an_embedded_newline_does_not_desync_its_neighbors(tmp_path):
    """`cat-file --batch`'s protocol is one revision per line. `-z` delivers
    a path holding a literal `\\n` as the real byte (fixing the round-1
    quoting bug), but writing that revision straight into the batch stream
    reads as TWO requests to git - which consumes the `readline()` meant for
    the next real entry, silently dropping it along with the odd path
    itself. Three files ordered `aaa.txt`, a newline-named file, `zzz.txt`:
    all three must read back correctly, not just the first.
    """
    _init_repo(tmp_path)
    _write(tmp_path, "aaa.txt", "aaa content\n")
    _write(tmp_path, "zzz.txt", "zzz content\n")
    _commit(tmp_path)
    _write(tmp_path, "aaa.txt", "aaa content 2\n")
    newline_rel = "dir/line1\nline2.txt"
    _write(tmp_path, newline_rel, "newline content\n")
    _write(tmp_path, "zzz.txt", "zzz content 2\n")
    _stage(tmp_path)

    paths = staged_content.staged_paths(root=tmp_path)
    assert "aaa.txt" in paths
    assert newline_rel in paths
    assert "zzz.txt" in paths

    blobs = dict(staged_content.read_index_blobs(paths, root=tmp_path))
    assert blobs["aaa.txt"] == "aaa content 2\n"
    assert blobs[newline_rel] == "newline content\n"
    assert blobs["zzz.txt"] == "zzz content 2\n"
