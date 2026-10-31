#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Lint gate: the variant links link everything the main link does.
#
# Two binaries come out of one compile on the Pi and x86 cross builds:
# $(TARGET) (mk/rules.mk) and $(FBDEV_TARGET) (mk/pi-dual-link.mk). The variant
# recipe links its own prerequisites, and that list is written out by hand, so an
# object added to the main link reaches the variant only if someone remembers to
# add it there too. Forgetting does not show in a native build or in a release
# that compiles the object out: it shows as undefined references in whichever
# configuration first compiles the object in (prestonbrown/helixscreen#1375).
#
# The check asks make itself. `make -pq` prints the rule database for a
# platform without building anything, and every object the main link depends
# on must also be a prerequisite of each variant link, unless it is one the
# variant deliberately replaces. Those replacements are named below by the make
# variable that lists them, so a new swap is declared once in the .mk file that
# performs it and only the variable name is repeated here.
#
# The platforms come from mk/cross.mk: every PLATFORM_TARGET block that turns on
# PI_DUAL_LINK is checked.
#
# Usage:
#   scripts/check_link_parity.py
#   scripts/check_link_parity.py --platform pi32-both
#   scripts/check_link_parity.py --extra-makefile seed.mk   # read after Makefile

from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from mutate_diff import make_bin  # noqa: E402

REPO_ROOT = Path(__file__).resolve().parent.parent

# Variant link -> (make variable naming the binary, {variable: why the variant
# leaves its objects out}). Every variable must exist in the platform's
# database; a renamed one fails rather than silently allowing nothing.
VARIANTS = {
    "fbdev": (
        "FBDEV_TARGET",
        {
            # Replaced by libhelix-display-fbdev.a, compiled without DRM.
            "DRM_DISPLAY_APP_OBJS": "display backend, rebuilt fbdev-only",
            # Replaced by the HELIX_BINARY_VARIANT="fbdev" compile.
            "DRM_CRASH_OBJ": "crash reporter, rebuilt with the fbdev variant name",
            # Replaced by the compiles without ENABLE_GLES_3D.
            "DRM_GLES_APP_OBJS": "GLES users, rebuilt with the CPU fallback",
            # The fbdev binary must not reference libdrm or libinput.
            "LVGL_DRM_DRIVER_OBJS": "LVGL DRM and libinput drivers",
            # The fbdev binary links no GPU libraries (mk/egl-link.mk).
            "EGL_PATH_OBJS": "EGL presentation path",
            # Runtime loaders for libraries the fbdev binary never calls.
            "IMPLIB_OBJS": "display and GPU library trampolines",
        },
    ),
}

PLATFORM_RE = re.compile(r"^\s*(?:else\s+)?ifeq\s*\(\$\(PLATFORM_TARGET\),([A-Za-z0-9_-]+)\)")
VARIANT_SWITCH_RE = re.compile(r"^\s*PI_DUAL_LINK\s*:?=\s*yes\b")


def variant_platforms(cross_mk: Path) -> list[str]:
    """PLATFORM_TARGET values whose block in mk/cross.mk turns on a variant link."""
    platforms: list[str] = []
    current = None
    for line in cross_mk.read_text().splitlines():
        m = PLATFORM_RE.match(line)
        if m:
            current = m.group(1)
            continue
        if line.strip() == "endif":
            current = None
            continue
        if current and VARIANT_SWITCH_RE.match(line) and current not in platforms:
            platforms.append(current)
    return platforms


def make_database(root: Path, platform: str, extra_makefiles: list[str]) -> str:
    """The rule and variable database for PLATFORM_TARGET=platform, built from nothing."""
    files = []
    for f in ["Makefile", *extra_makefiles]:
        files += ["-f", f]
    # The cross builds run on Linux; UNAME_S keeps a macOS host from adding its
    # Objective-C++ objects to the main link. MAKEFLAGS is cleared so a parent
    # make's command-line variables cannot leak into the platform evaluated here.
    env = {k: v for k, v in os.environ.items() if k not in ("MAKEFLAGS", "MAKELEVEL", "MFLAGS")}
    cmd = [make_bin(), "-pq", "--no-print-directory", *files,
           f"PLATFORM_TARGET={platform}", "UNAME_S=Linux", "print-var-TARGET"]
    r = subprocess.run(cmd, cwd=root, env=env, capture_output=True, text=True)
    # -q exits 1 because print-var-TARGET is never up to date; 2 is a make error.
    if r.returncode not in (0, 1):
        raise RuntimeError(f"{' '.join(cmd)} exited {r.returncode}:\n{r.stderr.strip()}")
    return r.stdout


def simple_variable(db: str, name: str) -> list[str] | None:
    """Words of a `:=` variable as printed in the database; None when it is absent."""
    m = re.search(rf"^{re.escape(name)} :?= ?(.*)$", db, re.M)
    if m is None:
        return None
    if "$(" in m.group(1):
        raise RuntimeError(f"{name} is recursively expanded; give it := so the gate can read it")
    return m.group(1).split()


def rule_prerequisites(db: str, target: str) -> list[str] | None:
    """Normal (not order-only) prerequisites of target's rule in the database."""
    m = re.search(rf"^{re.escape(target)}:(?!=)(.*)$", db, re.M)
    if m is None:
        return None
    return m.group(1).partition("|")[0].split()


def objects(words: list[str]) -> set[str]:
    return {w for w in words if w.endswith(".o")}


def check_platform(root: Path, platform: str, extra_makefiles: list[str]) -> tuple[list[str], list[str]]:
    """(findings, one summary line per variant checked) for one platform."""
    db = make_database(root, platform, extra_makefiles)
    findings: list[str] = []
    checked: list[str] = []

    main_target = (simple_variable(db, "TARGET") or [None])[0]
    main = rule_prerequisites(db, main_target) if main_target else None
    if main is None:
        return [f"{platform}: no rule for the main link $(TARGET) in the make database"], checked

    for variant, (target_var, swaps) in VARIANTS.items():
        target = (simple_variable(db, target_var) or [None])[0]
        if target is None:
            continue
        linked = rule_prerequisites(db, target)
        if linked is None:
            findings.append(f"{platform}: $({target_var}) is {target} but has no rule")
            continue

        allowed: set[str] = set()
        for var in swaps:
            words = simple_variable(db, var)
            if words is None:
                findings.append(f"{platform}: {variant} replacement list $({var}) is not defined")
                continue
            allowed |= objects(words)

        missing = sorted(objects(main) - objects(linked) - allowed)
        for obj in missing:
            findings.append(f"{platform}: {target} does not link {obj}, which {main_target} does")
        checked.append(f"{platform}/{variant}: {len(objects(linked))} objects, "
                       f"{len(allowed & objects(main))} replaced")
    return findings, checked


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--repo-root", default=str(REPO_ROOT))
    ap.add_argument("--platform", action="append",
                    help="PLATFORM_TARGET to check (repeatable; default: every variant platform in mk/cross.mk)")
    ap.add_argument("--extra-makefile", action="append", default=[],
                    help="makefile read after Makefile, for seeding a drift in tests")
    args = ap.parse_args()

    root = Path(args.repo_root)
    platforms = args.platform or variant_platforms(root / "mk/cross.mk")
    if not platforms:
        print("link parity: mk/cross.mk names no platform with a variant link", file=sys.stderr)
        return 1

    findings: list[str] = []
    checked: list[str] = []
    for platform in platforms:
        try:
            f, c = check_platform(root, platform, args.extra_makefile)
        except RuntimeError as e:
            findings.append(f"{platform}: {e}")
            continue
        findings += f
        checked += c

    if not checked and not findings:
        findings.append("no variant link was found on any platform: " + ", ".join(platforms))

    if findings:
        print("link parity: a variant link is missing objects the main link uses:", file=sys.stderr)
        for line in findings:
            print(f"  {line}", file=sys.stderr)
        print("Add the variable to the variant's link prerequisites, or, if the variant\n"
              "replaces those objects on purpose, list them in a replacement variable\n"
              "named in scripts/check_link_parity.py VARIANTS.", file=sys.stderr)
        return 1

    print(f"link parity: {len(checked)} variant links match their main link")
    for line in checked:
        print(f"  {line}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
