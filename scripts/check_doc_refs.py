#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Lint gate: agent-facing docs, docs/README.md (the public docs index), and
# docs/devel/ docs must not cite files that don't exist.
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
# Four checks:
#   refs   — every backticked path in an agent-facing doc resolves
#   links  — every markdown [text](target) link in a scanned doc resolves
#   index  — every doc in docs/devel/ is listed in docs/devel/CLAUDE.md
#   stale  — report-only: cites whose file changed after the doc last did
#
# A citation names a place — `src/printer/printer_state.cpp#update_from_status` —
# so the path is what this gate proves. Whether the NAME still exists is
# scripts/doc_anchors.py's question, asked by `make check-doc-anchors`.
#
# The index check is what makes lazy loading trustworthy: a doc missing from the
# routing table is a doc nobody will find.
#
# KNOWN GAP: a bare basename passes check_refs as soon as ANY file in the tree
#   shares it, submodules included — `file.cpp` in prose resolves to
#   lib/cpp-terminal/cpp-terminal/private/file.cpp. Harmless for a deliberate
#   placeholder, but it means the path check cannot be read as proof that the
#   file the sentence MEANT exists.
#
# Usage:
#   check_doc_refs.py            # everything: every CLAUDE.md, .claude/skills/,
#                                # docs/README.md, docs/devel/**/*.md,
#                                # refs+links, plus the index check
#   check_doc_refs.py --refs     # broken references only
#   check_doc_refs.py --index    # index completeness only
#   check_doc_refs.py --list     # show what was scanned
#   check_doc_refs.py --stale    # report-only: cites older than the code they
#                                # describe (release-time re-verify list)
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

# Pruned by PATH rather than by name, because the name alone is load-bearing
# elsewhere. `.claude` CANNOT go in SKIP_DIRS: the agent-doc walk deliberately
# collects `.claude/skills/*.md`, and skipping the whole directory would drop
# them silently. But `.claude/worktrees/` holds the harness's own ephemeral
# agent checkouts, each a complete copy of this repo — CLAUDE.md, docs/ and all.
# Walking those made every doc and every citation appear once per live agent:
# six copies of CLAUDE.md's `temperature_service.cpp:667`, anchored against a
# source tree that is not the one being checked. It went unnoticed for as long
# as the resolver had a nearest-to-old-line fallback to swallow them with, and
# turned main red the moment ambiguity became a hard failure. Deleting the
# worktrees is not the fix: they carry other agents' uncommitted work.
SKIP_PATHS = {os.path.join('.claude', 'worktrees')}


def prune_dirs(root, dirs, extra=()):
    """In-place prune of a walk's directory list, by name AND by path.

    Every walk in this file has to use it. A by-name prune cannot express
    "`.claude/skills` yes, `.claude/worktrees` no", and that distinction is the
    whole point.
    """
    rel = os.path.relpath(root, '.')
    keep = []
    for d in dirs:
        if d in SKIP_DIRS or d in extra:
            continue
        if os.path.normpath(os.path.join(rel, d)) in SKIP_PATHS:
            continue
        keep.append(d)
    dirs[:] = keep
    return dirs

# Paths that are intentionally absent from a clean checkout.
# NOTE: a developer machine has a built, patched tree, so these all resolve
# locally and only ever fail in CI - which checks out clean and does not build.
# That asymmetry is why main went red here for a day and a half without anyone
# reproducing it. Add to this list rather than rewording a doc: the citations
# are correct, the files simply do not exist yet at check time.
EXEMPT_SUBSTRINGS = (
    # Metasyntactic. scripts/CLAUDE.md documents the citation format itself
    # and has to show the shape; 'path/to/' cannot collide with a real file
    # the way a bare 'file.cpp' would (src/system/config_storage_file.cpp).
    'path/to/',
    'superpowers/',        # docs/superpowers/ is local-only working space; nothing
                           # there is tracked; refs to it are not resolvable on a
                           # fresh clone
    # Written at runtime.
    'settings-test.json',  # seeded by --test
    'config/settings.json',
    'release_info.json',   # written into INSTALL_DIR by the installer
    # Created by `make apply-patches` (patches/libhv-dns-resolver-fallback.patch).
    'dns_resolv.',
    # Build outputs.
    'compile_commands.json',
    'build/generated/',    # helix_git_hash.h and friends
    'MANIFEST.txt',        # written by genPackagingManifest at assets-build time
    # libhv BUILD PRODUCTS, cited bare by the build and ESP32 docs. libhv tracks
    # 375 files and not one of them is under include/ — its .gitignore ignores
    # that whole tree, because its public headers are assembled at build time.
    # `hv/requests.h`, `hv/hlog.h` and `dns_resolv.c` therefore exist on a
    # machine that has built once and in no fresh checkout, so the gate cannot
    # verify them anywhere it runs.
    'libhv/include/',
    'hv/requests.h',
    'hv/hlog.h',
    # Klipper extras in UPSTREAM repos, not ours. FILAMENT_BACKEND_MEDUSAHC.md
    # cites the two MedusaHC controllers by file and line because the schema
    # difference between them is the whole point of that section, and the cites
    # were read from those sources rather than inferred. Nothing named
    # medusahc.py will ever exist in this repo.
    'medusahc.py',
)

# Tokens that are obviously placeholders rather than real paths.
PLACEHOLDER_CHARS = ('<', '>', '*', '$', '…', '{')

# `some/path/file.ext` in prose or a table cell. The path may carry an anchor
# fragment (`#update_from_status`), a `:123` line, a `:123-145` RANGE, or a
# `:func_name()` symbol suffix; the path charset excludes ':' and '#' so no
# suffix can be part of a real path, and everything from the first ':' or '#'
# is stripped before the path is checked.
#
# `cfg` is deliberately absent from the extension list below. A `.cfg`
# citation in these docs is a Klipper config that lives on the PRINTER -
# `printer.cfg`, `box.cfg`, `Macros/toolchanger.cfg` - so admitting the
# extension would fail the path check on all of them for being correct.
# Checking device-side config needs a different mechanism, not this one.
CITED_EXTENSIONS = ('md', 'cpp', 'cc', 'h', 'hpp', 'c', 'xml', 'py', 'sh',
                    'json', 'mk', 'bats', 'yml', 'yaml', 'html', 'txt')
_EXT = '|'.join(CITED_EXTENSIONS)

PATH_RE = re.compile(
    r'`([A-Za-z0-9_./-]+\.(?:' + _EXT + r')'
    r'(?:#[^`\n]+|:\d+(?:[-–]\d+)?|:[A-Za-z0-9_]+\(\))?)`')

# Markdown [text](target) links. Link text must be non-empty — `[](...)` is a
# C++ lambda with a parenthesized parameter list (e.g. `.on_destroy = [](lv_obj_t*)`
# in a doc's code sample), never a markdown link. The anchor part (#+...) is
# optional and dropped; the target is resolved relative to the doc's own directory.
LINK_RE = re.compile(r'\[[^\]]+\]\(([^)#\s]+)(?:#[^)]*)?\)')

# A citation wrapped in a link — [`src/foo.cpp`](../../../src/foo.cpp) — is still
# a citation, so the path and staleness checks unwrap first. Without it, linking a
# doc would silently narrow what those checks see. Only a link around a code span
# is unwrapped; a prose link is left alone.
LINKED_SPAN_RE = re.compile(r'\[(`[^`\n]+`)\]\([^)\s]*\)')


def unwrap_links(text):
    return LINKED_SPAN_RE.sub(lambda m: m.group(1), text)


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
# 'plans' is the single tracked home for the in-flight set: dated
# implementation plans, their design specs, and the ESP32 program docs all
# live in docs/devel/plans/. They are written against the tree as it stood
# on their date, so their citations are historical record, not promises.
DEVEL_EXEMPT_SUBDIRS = ('plans', 'printer-research')

# Docs deliberately not routed from the index.
INDEX_EXEMPT = {
    'CLAUDE.md',           # the index itself
}

# Scanned with the agent-facing docs (same refs+links checks) although it is
# neither a CLAUDE.md nor a skill: docs/README.md is the public index every
# other docs/ page is reached through, so a dead link there is the first one
# a reader hits.
EXTRA_SCAN_DOCS = ('docs/README.md',)

# ...and so is every local page that index links. Listing a page there is what
# makes it live documentation, whatever directory it sits in: docs/audits/ and
# docs/user/ are reached only this way, and their citations went unchecked for
# as long as the set was a hand-written tuple. Deriving it means indexing a new
# page is the only edit needed to get it checked.
_INDEX_LINK_RE = re.compile(r'\[[^\]]+\]\(([^)\s]+)\)')


def indexed_docs(index=EXTRA_SCAN_DOCS[0]):
    """Every local .md the public docs index links to, that exists on disk."""
    try:
        with open(index, encoding='utf-8') as fh:
            text = fh.read()
    except OSError:
        return []
    base = os.path.dirname(index)
    out = []
    for m in _INDEX_LINK_RE.finditer(text):
        target = m.group(1).split('#')[0]
        # A scheme (https:, mailto:) names something outside the tree; only the
        # first path segment can carry one, so `a:b/c.md` is a URL and
        # `dir/a:b.md` is not.
        if not target.endswith('.md') or ':' in target.split('/')[0]:
            continue
        path = os.path.normpath(os.path.join(base, target))
        if os.path.exists(path):
            out.append(path)
    return out


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
        prune_dirs(root, dirs)
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
    def ask(cwd, candidates):
        if not candidates:
            return set()
        try:
            proc = subprocess.run(['git', 'check-ignore', '--stdin'],
                                  input='\n'.join(sorted(candidates)), capture_output=True,
                                  text=True, timeout=30, cwd=cwd or None)
        except (OSError, subprocess.SubprocessError):
            return set()  # no git, or it misbehaved: enforce strictly, as before
        return {line.strip() for line in proc.stdout.splitlines() if line.strip()}

    hits = ask(None, paths)

    # Then each populated submodule, against its OWN rules. A submodule's build
    # products are ignored by ITS .gitignore, which the superproject's git knows
    # nothing about — libhv gitignores its entire generated `include/` tree, so
    # every `lib/libhv/include/hv/*.h` the docs cite exists only after a build
    # and never in a CI checkout. Asking the superproject alone marked all of
    # them stale.
    for sub in submodule_paths():
        if not os.path.isdir(sub):
            continue
        inside = {p[len(sub) + 1:]: p for p in paths
                  if p.startswith(sub + '/') and p not in hits}
        if not inside:
            continue
        for rel in ask(sub, set(inside)):
            hits.add(inside[rel])
    return hits


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
    """Agent-facing docs: every CLAUDE.md, everything under .claude/skills/,
    plus the extra scanned docs (the public docs index)."""
    targets = []
    for root, dirs, files in os.walk('.'):
        prune_dirs(root, dirs)
        rel = root[2:]
        if rel.startswith('lib/'):
            continue
        for f in files:
            path = os.path.join(rel, f) if rel else f
            if f == 'CLAUDE.md' or (rel.startswith('.claude/skills') and f.endswith('.md')):
                targets.append(path)
    targets.extend(EXTRA_SCAN_DOCS)
    targets.extend(indexed_docs())
    return sorted(set(targets))


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
            prune_dirs(dirpath, dirs, extra=DEVEL_EXEMPT_SUBDIRS)
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
    # Basename index so a bare `THREADING.md` resolves without scanning all
    # ~70k repo paths per citation — the suffix fallback used to dominate the
    # whole run (8s of the gate's cost). Refs containing '/' keep the exact
    # suffix scan (p.endswith('/'+path)); those are rare.
    by_basename = {}
    for p in allpaths:
        by_basename.setdefault(p.rsplit('/', 1)[-1], []).append(p)
    allpaths_set = allpaths if isinstance(allpaths, set) else set(allpaths)
    for target in targets:
        base = os.path.dirname(target)
        try:
            text = unwrap_links(open(target, errors='ignore').read())
        except OSError:
            continue
        for m in PATH_RE.finditer(text):
            ref = m.group(1)
            # Everything from the first '#' or ':' is a suffix naming a place
            # INSIDE the file, never part of the path.
            path = ref.split('#', 1)[0].split(':', 1)[0]
            if any(c in path for c in PLACEHOLDER_CHARS):
                continue
            if any(s in ref for s in EXEMPT_SUBSTRINGS):
                continue
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
            if '/' in path:
                if any(p.endswith('/' + path) for p in allpaths):
                    continue
            elif path in by_basename:
                continue
            if path in allpaths_set:
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


def last_commit_dates(paths):
    """Newest commit date (ISO) per path, from ONE streaming `git log` walk.

    Spawning `git log -1 -- <path>` per citation made --stale take 100s+ (a
    process spawn per cited file, several hundred of them). Instead we stream
    full history newest-first with --name-only and record the first commit
    each wanted path appears in — then stop the walk as soon as every wanted
    path has been seen. Docs cite living code, so the walk almost always ends
    within a few hundred commits of HEAD.
    """
    import subprocess
    wanted = set(paths)
    found = {}
    if not wanted:
        return found
    try:
        proc = subprocess.Popen(
            ['git', 'log', '--format=\x01%cI', '--name-only', '--no-renames'],
            stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True,
            errors='ignore')
    except OSError:
        return found
    date = None
    try:
        for line in proc.stdout:
            line = line.rstrip('\n')
            if line.startswith('\x01'):
                date = line[1:]
                continue
            if not line or date is None:
                continue
            if line in wanted and line not in found:
                found[line] = date
                if len(found) == len(wanted):
                    proc.kill()
                    break
    finally:
        proc.stdout.close()
        proc.wait()
    return found


def check_stale(targets, devel=False):
    """Release-time report: cites whose file moved after the doc last did.

    Not a gate. A resolved cite can still be wrong (the line/symbol checks
    catch the worst of that), and a count table can be silently out of date —
    both rot exactly when the cited tree changes after the doc was written.
    This names those pairs so a release audit starts from a list instead of a
    fan-out. Docs carrying a "recounted <date>" census annotation are compared
    against that date too: a recount older than the cited tree's last change
    is the count-drift the .115 audit kept finding.
    """
    RECOUNT_RE = re.compile(r'recounted (\d{4}-\d{2}-\d{2})')
    PATH_ONLY_RE = re.compile(
        r'`([A-Za-z0-9_./-]+\.(?:cpp|cc|h|hpp|c|xml|py|sh|json|mk|bats|yml|'
        r'yaml|html|txt))(?:#[^`\n]+|:\d+|:[A-Za-z0-9_]+\(\))?`')
    docs = {}
    for target in targets:
        try:
            text = unwrap_links(open(target, errors='ignore').read())
        except OSError:
            continue
        docs[target] = text
    all_cited = set()
    for target, text in docs.items():
        all_cited.update(m.group(1) for m in PATH_ONLY_RE.finditer(text)
                         if not any(s in m.group(1) for s in EXEMPT_SUBSTRINGS))
    dates = last_commit_dates(all_cited | set(docs))
    reports = []
    for target, text in docs.items():
        doc_date = dates.get(target)
        if not doc_date:
            continue
        recounts = RECOUNT_RE.findall(text)
        cited = sorted({m.group(1) for m in PATH_ONLY_RE.finditer(text)
                        if not any(s in m.group(1) for s in EXEMPT_SUBSTRINGS)})
        for ref in cited:
            fdate = dates.get(ref)
            if not fdate:
                continue
            if fdate[:10] > doc_date[:10]:
                reports.append('%s cites `%s` — file last changed %s, doc %s'
                               % (target, ref, fdate[:10], doc_date[:10]))
            elif recounts and fdate[:10] > max(recounts):
                reports.append('%s recounts %s but `%s` changed %s — recount '
                               'is stale' % (target, max(recounts), ref,
                                             fdate[:10]))
    return reports


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--refs', action='store_true', help='Only check references resolve')
    ap.add_argument('--index', action='store_true', help='Only check index completeness')
    ap.add_argument('--devel', nargs='*', dest='devel_paths', metavar='PATH',
                    help='Check docs/devel/**/*.md (or only the given .md files '
                         'and directories): path refs and markdown links')
    ap.add_argument('--stale', action='store_true',
                    help='Report-only: cites whose file changed after the doc '
                         'last did (release-time re-verification list)')
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

    if args.stale:
        reports = check_stale(targets, devel=devel)
        if reports:
            print('⚠️  Citations older than the code they describe (%d):' % len(reports))
            for r in reports:
                print('   %s' % r)
            print('   Re-verify these before a release; not a commit-time failure.')
        else:
            print('✅ No citation is older than its doc (%d files scanned)' % len(targets))
        return 0

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
