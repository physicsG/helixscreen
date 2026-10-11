#!/usr/bin/env python3
"""Derive the patch marker table (mk/patch-markers.tsv) from wired patches.

A marker is one line a patch introduces (or removes) that upstream never
contained. Checking it is a plain text search, so it stays answerable where
git is not - a docker build rsynced from a worktree has no readable submodule
repository, but it still has the files, and "is this line in the file" does
not need anything else.

Derivation rules, applied to every file each patch touches (one row per
file, so a half-applied patch cannot pass on the file that did apply):
  - candidates are the patch's added lines, newest first (a regenerated patch
    appends hunks, so the last line is the one an older revision lacks), then
    its removed lines, newest first;
  - an added line qualifies when the patched checkout contains it and the
    pinned upstream HEAD does not (a line upstream also has can never prove
    the patch is present);
  - a removed line qualifies when upstream contains it and the patched
    checkout does not;
  - a candidate claimed by more than one patch is dropped - it could not be
    attributed, and a check that another patch can satisfy is worse than no
    check (it reads as coverage and verifies nothing);
  - lines shorter than 12 characters or containing tabs are not distinctive
    enough to be a marker.

The script refuses rather than emitting a partial table: a patch with no
eligible marker must gain one (or the rule must change) - never a silent gap.

Requires a git-readable tree: upstream content is read from the submodule's
pinned HEAD. The runtime check (scripts/check_patch_markers.py) deliberately
requires nothing but the files themselves.
"""
import argparse
import hashlib
import re
import subprocess
import sys

STANZA_RE = re.compile(
    r"\$\(Q\)\$\(APPLY_PATCH\) \$\((LVGL_DIR|LIBHV_DIR|LUA_DIR)\) "
    r"\$\(PATCH_DIR\)/([A-Za-z0-9_.-]+\.patch)"
)
QUOTED_RE = re.compile(r'"([^"]*)"')
MIN_MARKER_LEN = 12
# Preprocessor conditionals are structure any patch can add; they carry no
# evidence about which patch is present.
NON_MARKER_RE = re.compile(r"#(if|ifdef|ifndef|elif|else|endif)\b")

COLUMNS = ("sha256", "patch", "kind", "dir", "file", "marker", "label", "note")


def upstream(submodule, path):
    """Content of a file at the submodule's pinned HEAD, or None if absent."""
    result = subprocess.run(
        ["git", "-C", submodule, "show", f"HEAD:{path}"],
        capture_output=True, text=True,
    )
    return result.stdout if result.returncode == 0 else None


def parse_patch(path):
    """Yield (kind, file, text) for every line the patch adds or removes."""
    current = None
    files = []
    with open(path, encoding="utf-8", errors="replace") as fh:
        for line in fh:
            m = re.match(r"diff --git a/\S+ b/(\S+)", line)
            if m:
                current = m.group(1)
                files.append(current)
                continue
            m = re.match(r"\+\+\+ b/(\S+)", line)
            if m:
                # Plain diff headers carry no diff --git line.
                current = m.group(1)
                if current not in files:
                    files.append(current)
                continue
            if line.startswith("@@") or current is None:
                continue
            if line.startswith("+"):
                yield ("+", current, line[1:].strip())
            elif line.startswith("-"):
                yield ("-", current, line[1:].strip())


def touched_files(path):
    """Files the patch changes, in patch order, excluding deletions."""
    files = []
    for _, name, _ in parse_patch(path):
        if name not in files:
            files.append(name)
    return files


def working(submodule, path):
    try:
        with open(f"{submodule}/{path}", encoding="utf-8", errors="replace") as fh:
            return fh.read()
    except OSError:
        return None


def stanzas(mk_path):
    """Wired apply stanzas in make order: (dir var, patch name, label, note)."""
    out = []
    with open(mk_path, encoding="utf-8") as fh:
        for line in fh:
            m = STANZA_RE.search(line)
            if not m:
                continue
            quoted = QUOTED_RE.findall(line[m.end():])
            label = quoted[0] if quoted else m.group(2)
            note = quoted[1] if len(quoted) > 1 else ""
            out.append((m.group(1), m.group(2), label, note))
    return out


def candidates(patch_path, submodule):
    """Eligible (kind, file, text) candidates, newest first."""
    added, removed = [], []
    for kind, path, text in parse_patch(patch_path):
        if len(text) < MIN_MARKER_LEN or "\t" in text:
            continue
        if NON_MARKER_RE.match(text):
            continue
        patched = working(submodule, path)
        pristine = upstream(submodule, path)
        if kind == "+":
            if patched is not None and text in patched \
                    and (pristine is None or text not in pristine):
                added.append((kind, path, text))
        else:
            if patched is not None and pristine is not None \
                    and text in pristine and text not in patched:
                removed.append((kind, path, text))
    return added + removed


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--mk", default="mk/patches.mk")
    ap.add_argument("--tsv", default="mk/patch-markers.tsv")
    ap.add_argument("--patch-dir", default="patches")
    ap.add_argument("--lvgl", default="lib/lvgl")
    ap.add_argument("--libhv", default="lib/libhv")
    ap.add_argument("--lua", default="lib/lua")
    ap.add_argument("--write", action="store_true",
                    help="write the table instead of printing it")
    args = ap.parse_args()
    dirs = {"LVGL_DIR": args.lvgl, "LIBHV_DIR": args.libhv, "LUA_DIR": args.lua}

    wired = stanzas(args.mk)
    if not wired:
        sys.exit(f"{args.mk}: no wired apply stanzas found")

    by_patch = {}
    for var, name, _, _ in wired:
        by_patch[name] = candidates(f"{args.patch_dir}/{name}", dirs[var])

    # A candidate two patches could satisfy attributes to neither.
    owners = {}
    for name, cands in by_patch.items():
        for cand in set(cands):
            owners.setdefault(cand, set()).add(name)

    # One marker per file the patch touches: a single marker proves only its
    # own file, so a half-applied patch whose other files were reverted would
    # read as applied (prestonbrown/helixscreen#1681).
    rows, refused = [], []
    for var, name, label, note in wired:
        patch_path = f"{args.patch_dir}/{name}"
        digest = hashlib.sha256(open(patch_path, "rb").read()).hexdigest()
        for path in touched_files(patch_path):
            pick = next((c for c in by_patch[name]
                         if c[1] == path and len(owners[c]) == 1), None)
            if pick is None:
                refused.append(f"{name} ({path})")
                continue
            kind, _, text = pick
            rows.append([digest, name, kind, var, path, text, label, note])

    if refused:
        sys.exit(
            "no eligible marker for: "
            + ", ".join(refused)
            + "\nEach file a patch touches needs one line of at least "
            f"{MIN_MARKER_LEN} characters that upstream does not contain."
        )

    lines = ["\t".join(COLUMNS)]
    lines += ["\t".join(row) for row in rows]
    table = "\n".join(lines) + "\n"
    if args.write:
        with open(args.tsv, "w", encoding="utf-8") as fh:
            fh.write(table)
        print(f"wrote {args.tsv}: {len(rows)} markers")
    else:
        sys.stdout.write(table)


if __name__ == "__main__":
    main()
