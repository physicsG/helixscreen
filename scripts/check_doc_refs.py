#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Lint gate: agent-facing docs and docs/devel/ docs must not cite files that
# don't exist.
#
# CLAUDE.md files and skills work by progressive disclosure — they are mostly
# pointers, and a pointer to a renamed or deleted file is worse than no pointer.
# It sends the reader (human or agent) looking for something that isn't there, and
# it silently teaches a wrong name. Several rounds of this have been cleaned up by
# hand:
#
#   - .claude/checklist.md cited six docs that had moved to docs/devel/
#   - CLAUDE.md taught ui_nav_push_overlay(), a function with zero call sites
#   - scripts/CLAUDE.md documented package.sh, deleted in 1ddbbbdba
#   - a lesson taught lv_xml_component_register_from_file(), a transposed name
#     that exists nowhere
#
# Three checks:
#   refs   — every backticked path in an agent-facing doc resolves
#   links  — every markdown [text](target) link in a scanned doc resolves
#   index  — every doc in docs/devel/ is listed in docs/devel/CLAUDE.md
#
# The index check is what makes lazy loading trustworthy: a doc missing from the
# routing table is a doc nobody will find.
#
# Usage:
#   check_doc_refs.py            # everything: agent docs + docs/devel/**/*.md,
#                                # refs+links, plus the docs/devel index check
#   check_doc_refs.py --refs     # broken references only
#   check_doc_refs.py --index    # index completeness only
#   check_doc_refs.py --list     # show what was scanned
#   check_doc_refs.py --devel [PATHS...]
#                                # refs+links over docs/devel/**/*.md, or only
#                                # the given PATHS (.md files, or directories
#                                # walked for .md). Point-in-time subdirs
#                                # (plans/, printer-research/) are exempt.

import argparse
import os
import subprocess
import re
import sys

SKIP_DIRS = {'.git', '.worktrees', 'build', 'node_modules', '.venv', 'venv'}

# Paths that are intentionally absent from a clean checkout.
EXEMPT_SUBSTRINGS = (
    'superpowers/',        # docs/superpowers/ specs are gitignored, local-only
)

# Tokens that are obviously placeholders rather than real paths.
PLACEHOLDER_CHARS = ('<', '>', '*', '$', '…', '{')

# `some/path/file.ext` in prose or a table cell. The path may carry a `:123`
# line or a `:func_name()` symbol suffix; the path charset excludes ':' so the
# suffix can never be part of a real path and is stripped before checking.
PATH_RE = re.compile(
    r'`([A-Za-z0-9_./-]+\.(?:md|cpp|cc|h|hpp|c|xml|py|sh|json|mk|bats|yml|yaml|html|txt)'
    r'(?::\d+|:[A-Za-z0-9_]+\(\))?)`')

# Markdown [text](target) links. Link text must be non-empty — `[](...)` is a
# C++ lambda with a parenthesized parameter list (e.g. `.on_destroy = [](lv_obj_t*)`
# in a doc's code sample), never a markdown link. The anchor part (#+...) is
# optional and dropped; the target is resolved relative to the doc's own directory.
LINK_RE = re.compile(r'\[[^\]]+\]\(([^)#\s]+)(?:#[^)]*)?\)')

# Link targets that cannot be verified on disk.
LINK_SKIP_PREFIXES = ('http://', 'https://', 'mailto:', '#')
LINK_SKIP_SUFFIXES = ('.d2', '.png', '.jpg', '.jpeg', '.gif', '.svg', '.webp',
                      '.bmp', '.ico')

DOC_INDEX = 'docs/devel/CLAUDE.md'
DOC_DIR = 'docs/devel'

# Point-in-time docs under docs/devel/ — dated plans and device research notes
# whose citations rot by design. Matched as directory-name components during a
# walk, so a scan rooted anywhere (meta-test fixture, targeted run) exempts a
# plans/ or printer-research/ subdir the same way the default walk does.
#
# 'plans' deliberately covers BOTH docs/devel/plans/ and docs/devel/specs/plans/:
# both hold dated implementation plans written against the tree as it stood on
# their date, so their citations are historical record, not promises. The specs
# directly under docs/devel/specs/ ARE scanned — design docs stay live long
# enough to owe the reader resolving paths.
DEVEL_EXEMPT_SUBDIRS = ('plans', 'printer-research')

# Docs deliberately not routed from the index.
INDEX_EXEMPT = {
    'CLAUDE.md',           # the index itself
}


def repo_files():
    """Every file in the repo, for suffix resolution.

    followlinks=True because setup-worktree.sh symlinks the lib/ submodules back
    into the main tree. Without it a worktree indexes none of them, and a doc
    citing a submodule file (lv_sdl_window.c in the patch workflow) reads as
    broken there while resolving fine on the main tree. `seen` guards against a
    symlink cycle, which followlinks would otherwise recurse into forever.
    """
    out = set()
    seen = set()
    for root, dirs, files in os.walk('.', followlinks=True):
        dirs[:] = [d for d in dirs if d not in SKIP_DIRS]
        keep = []
        for d in dirs:
            real = os.path.realpath(os.path.join(root, d))
            if real in seen:
                continue
            seen.add(real)
            keep.append(d)
        dirs[:] = keep
        for f in files:
            out.add(os.path.join(root, f)[2:])
    return out


def gitignored(paths):
    """Which of @p paths git deliberately keeps out of the tree.

    A reference to a generated file (compile_commands.json,
    build/generated/helix_git_hash.h, the config/settings*.json a first run
    writes) is a CORRECT path that simply cannot exist in a clean checkout. It
    resolves on a developer machine that has built once and never in CI, so
    asserting its absence is a broken link asserts the wrong thing -- and did:
    four of these failed every CI run while passing every local one.

    Batched through one `git check-ignore --stdin` rather than a call per path;
    it exits 1 when nothing matches, which is not an error here.
    """
    if not paths:
        return set()
    try:
        proc = subprocess.run(['git', 'check-ignore', '--stdin'],
                              input='\n'.join(sorted(paths)), capture_output=True,
                              text=True, timeout=30)
    except (OSError, subprocess.SubprocessError):
        return set()  # no git, or it misbehaved: enforce strictly, as before
    return {line.strip() for line in proc.stdout.splitlines() if line.strip()}


def submodule_paths():
    """Declared submodule paths, whether or not they are populated."""
    if not os.path.isfile('.gitmodules'):
        return []
    return [m.group(1) for m in
            re.finditer(r'^\s*path\s*=\s*(.+?)\s*$', open('.gitmodules').read(), re.M)]


def uninitialized_submodules():
    """Submodule paths that are declared but not checked out.

    CI checks out the superproject without submodules, so lib/lvgl and friends are
    empty there while they are fully populated on a developer machine. A doc that
    legitimately cites a submodule file (e.g. lv_sdl_window.c in the patch workflow)
    would resolve locally and fail in CI. We cannot verify a file that is not on
    disk, so we report those separately instead of asserting they are broken.
    """
    missing = []
    if not os.path.isfile('.gitmodules'):
        return missing

    # `git submodule status` is the authority: it prefixes an uninitialized
    # entry with '-'. The emptiness test below cannot see a PARTIAL checkout,
    # and that is the state CI actually lands in — a submodule init that fails
    # and retries leaves the directory non-empty (a .git file, a few objects)
    # while the referenced sources are absent. The gate then read those refs as
    # stale and failed, which is exactly what it happened to do for the four
    # lib/libhv citations.
    try:
        proc = subprocess.run(['git', 'submodule', 'status'],
                              capture_output=True, text=True, timeout=60)
        if proc.returncode == 0 and proc.stdout.strip():
            for line in proc.stdout.splitlines():
                if line.startswith('-'):
                    parts = line[1:].split()
                    if len(parts) >= 2:
                        missing.append(parts[1])
            return missing
    except (OSError, subprocess.SubprocessError):
        pass  # fall through to the on-disk heuristic

    for m in re.finditer(r'^\s*path\s*=\s*(.+?)\s*$', open('.gitmodules').read(), re.M):
        sub = m.group(1)
        if not os.path.isdir(sub) or not os.listdir(sub):
            missing.append(sub)
    return missing


def scan_targets():
    """Agent-facing docs: every CLAUDE.md, plus everything under .claude/skills/."""
    targets = []
    for root, dirs, files in os.walk('.'):
        dirs[:] = [d for d in dirs if d not in SKIP_DIRS]
        rel = root[2:]
        if rel.startswith('lib/'):
            continue
        for f in files:
            path = os.path.join(rel, f) if rel else f
            if f == 'CLAUDE.md' or (rel.startswith('.claude/skills') and f.endswith('.md')):
                targets.append(path)
    return sorted(targets)


def scan_devel_targets(paths):
    """--devel mode targets: docs/devel/**/*.md, or only the given paths.

    An explicit .md path is scanned as-is (even one under an exempt subdir — an
    explicit request is explicit). A directory is walked for .md files the same
    way the default docs/devel walk is, exemptions included. Anything else is
    dropped.
    """
    def walk_md(root):
        out = []
        for dirpath, dirs, files in os.walk(root):
            dirs[:] = [d for d in dirs
                       if d not in SKIP_DIRS and d not in DEVEL_EXEMPT_SUBDIRS]
            for f in files:
                if f.endswith('.md'):
                    out.append(os.path.join(dirpath, f))
        return out

    if paths:
        targets = []
        for p in paths:
            if os.path.isdir(p):
                targets.extend(walk_md(p))
            elif p.endswith('.md'):
                targets.append(p)
        return sorted(set(targets))
    return sorted(walk_md(DOC_DIR))


def check_refs(targets, allpaths, devel=False):
    problems = []
    for target in targets:
        base = os.path.dirname(target)
        try:
            text = open(target, errors='ignore').read()
        except OSError:
            continue
        for m in PATH_RE.finditer(text):
            ref = m.group(1)
            if any(c in ref for c in PLACEHOLDER_CHARS):
                continue
            if any(s in ref for s in EXEMPT_SUBSTRINGS):
                continue
            path = ref.split(':', 1)[0]  # strip a :123 / :func_name() suffix
            if os.path.exists(path):
                continue
            if base and os.path.exists(os.path.join(base, path)):
                continue
            if devel and base:
                # A devel doc cites repo-rooted paths (src/…, include/…). In the
                # repo those resolve from the cwd above; a scratch tree handed to
                # --devel has the same shape one level above the doc's directory
                # (<root>/devel/doc.md citing <root>/src/…).
                parent = os.path.dirname(base)
                if parent and os.path.exists(os.path.join(parent, path)):
                    continue
            # a bare or partial path is fine if exactly that suffix exists somewhere
            if any(p == path or p.endswith('/' + path) for p in allpaths):
                continue
            line = text.count('\n', 0, m.start()) + 1
            problems.append((target, line, ref))
    return problems


def check_links(targets):
    """Markdown links must resolve relative to the doc's own directory.

    http(s)/mailto targets, bare #anchors, and .d2/image references cannot be
    verified on disk and are skipped.
    """
    problems = []
    for target in targets:
        base = os.path.dirname(target)
        try:
            text = open(target, errors='ignore').read()
        except OSError:
            continue
        for m in LINK_RE.finditer(text):
            ref = m.group(1)
            if ref.startswith(LINK_SKIP_PREFIXES):
                continue
            if ref.lower().endswith(LINK_SKIP_SUFFIXES):
                continue
            if os.path.exists(os.path.join(base, ref) if base else ref):
                continue
            line = text.count('\n', 0, m.start()) + 1
            problems.append((target, line, ref))
    return problems


def check_index():
    if not os.path.isfile(DOC_INDEX):
        return [], []
    index_text = open(DOC_INDEX, errors='ignore').read()
    present = set()
    for f in os.listdir(DOC_DIR):
        full = os.path.join(DOC_DIR, f)
        if os.path.isfile(full) and (f.endswith('.md') or f.endswith('.html')):
            present.add(f)
    unindexed = sorted(f for f in present - INDEX_EXEMPT
                       if '`%s`' % f not in index_text)
    return unindexed, sorted(present)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--refs', action='store_true', help='Only check references resolve')
    ap.add_argument('--index', action='store_true', help='Only check index completeness')
    ap.add_argument('--devel', nargs='*', dest='devel_paths', metavar='PATH',
                    help='Check docs/devel/**/*.md (or only the given .md files '
                         'and directories): path refs and markdown links')
    ap.add_argument('--list', action='store_true', help='List scanned files')
    args = ap.parse_args()

    devel = args.devel_paths is not None
    if devel:
        targets = scan_devel_targets(args.devel_paths)
        do_refs = True
        do_index = False
    else:
        # Default scope = agent-facing docs + the docs/devel target set. The
        # docs/devel half used to be opt-in via --devel; it flipped to default
        # enforcement once the surviving docs were swept clean, so CI catches
        # future citation rot in feature docs instead of accumulating it.
        targets = sorted(set(scan_targets()) | set(scan_devel_targets([])))
        # devel=True enables the parent-dir resolution fallback for docs/devel
        # citations; for repo-root CLAUDE.md files it is a harmless no-op.
        devel = True
        do_refs = args.refs or not args.index
        do_index = args.index or not args.refs

    if args.list:
        for t in targets:
            label = 'scanned (devel)' if t.startswith('docs/devel') else 'scanned'
            print('  %s: %s' % (label, t))

    exit_code = 0

    if do_refs:
        problems = check_refs(targets, repo_files(), devel=devel)
        link_problems = check_links(targets)

        # Split the findings into "cannot be checked here" and "genuinely broken"
        # instead of suppressing the whole gate when either applies.
        #
        # It used to be all-or-nothing: ANY uninitialized submodule downgraded
        # EVERY finding to a warning. That made the gate fail in two opposite
        # ways at once. In the job that does populate submodules it enforced
        # strictly and flagged 18 paths that were all either generated or inside
        # lib/libhv -- none of them stale. In the job that does not, it went
        # warn-only and returned 0, so the meta-tests asserting a dead path exits
        # 1 failed. Attributing each finding fixes both: a reference into an
        # unpopulated submodule, or to a path git deliberately ignores, is
        # unverifiable here; anything else is a real break and still fails.
        unpopulated = set(uninitialized_submodules())
        subs = submodule_paths()

        def _ref_path(ref):
            return ref.split(':', 1)[0]

        candidates = {_ref_path(r) for _, _, r in problems + link_problems}
        ignored = gitignored(candidates)

        def unverifiable(ref):
            path = _ref_path(ref)
            if path in ignored:
                return True
            # Inside a submodule that is not checked out here. Matched on the
            # declared submodule path, and also on a bare suffix reference
            # (`dns_resolv.c`), which is how the docs cite files inside one.
            for sub in subs:
                if sub in unpopulated and (path.startswith(sub + '/') or path == sub):
                    return True
            if unpopulated:
                for sub in unpopulated:
                    base = os.path.basename(sub)
                    if path.startswith(base + '/'):
                        return True
            return False

        # Repo-wide scanning with submodules missing keeps the old lenient
        # behaviour for anything left over: the docs cite submodule interiors by
        # bare name (`lv_sdl_window.c`, `dns_resolv.c`), which cannot be
        # attributed to a submodule by path shape, and failing a developer's
        # `make check` because they have not run `git submodule update` is the
        # noise the escape hatch was added to prevent.
        #
        # Explicit --devel PATHS stay STRICT regardless. That is the mode the
        # meta-tests use, and "check exactly these files" has to mean it — the
        # blanket downgrade is precisely why a dead path returned 0 in the BATS
        # job and the gate's own tests failed.
        explicit = bool(args.devel_paths)
        lenient = bool(unpopulated) and not explicit

        def _skip(problem):
            return unverifiable(problem[2]) or lenient

        unverified = [p for p in problems + link_problems if _skip(p)]
        problems = [p for p in problems if not _skip(p)]
        link_problems = [p for p in link_problems if not _skip(p)]

        if unverified:
            print('⚠️  Doc references not verifiable in this checkout '
                  '(generated, or inside an uninitialized submodule):')
            for target, line, ref in unverified:
                print('   %s:%d: `%s`' % (target, line, ref))
        if problems:
            print('❌ Doc references that do not resolve:')
            for target, line, ref in problems:
                print('   %s:%d: `%s`' % (target, line, ref))
            print('   Fix the path, or use a <placeholder> if it is illustrative.')
            exit_code = 1
        else:
            print('✅ Doc references: all resolve (%d files scanned)' % len(targets))
        if link_problems:
            print('❌ Doc links that do not resolve:')
            for target, line, ref in link_problems:
                print('   %s:%d: `%s`' % (target, line, ref))
            print('   Fix the target, or use a full URL if it is not in-tree.')
            exit_code = 1
        else:
            print('✅ Doc links: all resolve (%d files scanned)' % len(targets))
    if do_index:
        unindexed, present = check_index()
        if unindexed:
            print('❌ Docs in %s/ missing from %s:' % (DOC_DIR, DOC_INDEX))
            for f in unindexed:
                print('   %s' % f)
            print('   Add a row, or add to INDEX_EXEMPT if it is deliberately unrouted.')
            exit_code = 1
        else:
            print('✅ Doc index: all %d docs routed' % len(present))

    return exit_code


if __name__ == '__main__':
    sys.exit(main())
