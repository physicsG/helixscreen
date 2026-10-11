#!/usr/bin/env python3
"""Fail when a plugin translation pack shadows a system string.

Packs are consulted BEFORE the app catalog, so a pack entry whose tag matches
a catalog key does not "also translate" that string - it overrides the
system's translation for the whole UI while the plugin is loaded (a plugin
pack's "Start" relabels every Start button). The rule: a pack may only carry
strings the app does not already say. Checked against translations/<lang>.yml
(the source of truth, per qc_base_locale), not the generated XML.

Usage: check_plugin_i18n_shadow.py [--root DIR] [--staged-only]
With --root, scan plugin packs under DIR/i18n instead of the repository's
examples and fixtures (used by the meta-test).
"""

from __future__ import annotations

import argparse
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent


def catalog_tags(lang: str) -> set[str] | None:
    """English-source keys for one language, from translations/<lang>.yml.

    The YAML is flat "English text: translation" lines; keys are the tags.
    """
    yml = REPO / "translations" / f"{lang}.yml"
    if not yml.exists():
        return None
    tags: set[str] = set()
    for line in yml.read_text(encoding="utf-8").splitlines():
        # Single-line keys under `translations:`: quoted or bare, indented.
        m = re.match(r"^\s+(?:'([^']*)'|\"([^\"]*)\"|([^:#']+))\s*:", line)
        if m:
            tags.add(next(g for g in m.groups() if g is not None))
    return tags


def pack_tags(pack: Path) -> list[tuple[str, str]]:
    """(language, tag) for every translation entry in one pack file."""
    root = ET.parse(pack).getroot()
    langs = (root.get("languages") or "").split()
    out = []
    for tr in root.iter("translation"):
        tag = tr.get("tag")
        if tag:
            for lang in langs:
                out.append((lang, tag))
    return out


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", type=Path, default=None,
                    help="scan <root>/i18n/*.xml as one plugin instead of the repo's")
    args = ap.parse_args()

    if args.root:
        packs = sorted(args.root.glob("i18n/*.xml"))
    else:
        packs = sorted(REPO.glob("examples/plugins/*/i18n/*.xml"))
        packs += sorted(REPO.glob("tests/fixtures/plugins/*/i18n/*.xml"))

    def shown(p: Path) -> str:
        try:
            return str(p.relative_to(REPO))
        except ValueError:
            return str(p)

    catalogs: dict[str, set[str] | None] = {}
    shadows: list[str] = []
    for pack in packs:
        for lang, tag in pack_tags(pack):
            if lang not in catalogs:
                catalogs[lang] = catalog_tags(lang)
            cat = catalogs[lang]
            if cat is None:
                shadows.append(f"{shown(pack)}: language '{lang}' has no "
                               f"translations/{lang}.yml to check against")
            elif tag in cat:
                shadows.append(f"{shown(pack)}: '{tag}' shadows the app "
                               f"catalog's own '{lang}' string")

    if shadows:
        print("Plugin packs must not shadow system strings:", file=sys.stderr)
        for s in shadows:
            print(f"  {s}", file=sys.stderr)
        print("Packs are looked up before the catalog, so a duplicate tag overrides "
              "the app's translation app-wide while the plugin is loaded. Remove the "
              "entry; the catalog already translates it.", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
