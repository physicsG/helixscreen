#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Fail when src/ creates an XML component by a name nothing can register.

lv_xml_create(parent, "name", attrs) on an unknown name logs an error and
returns NULL, so the row or card it was meant to build is silently missing.

Registrable means one of:
  - ui_xml/<name>.xml or ui_xml/components/<name>.xml exists (registered on
    first use);
  - a "<name>.xml" string literal in src/ (a path passed to
    lv_xml_register_component_from_file());
  - lv_xml_register_widget("<name>", ...) in src/;
  - an lv_* name (LVGL's built-in widgets).
"""

import argparse
import re
import sys
from pathlib import Path

CREATE = re.compile(r'lv_xml_create\s*\([^;]*?,\s*"([A-Za-z_][A-Za-z0-9_]*)"', re.S)
XML_FILE = re.compile(r'"(?:[^"]*/)?([A-Za-z_][A-Za-z0-9_]*)\.xml"')
WIDGET = re.compile(r'lv_xml_register_widget\(\s*"([A-Za-z_][A-Za-z0-9_]*)"')


def scan(root):
    sources = [p for p in (root / 'src').rglob('*') if p.suffix in ('.cpp', '.h', '.c')]
    registered, created = set(), []
    for d in ('ui_xml', 'ui_xml/components'):
        registered.update(p.stem for p in (root / d).glob('*.xml'))
    for path in sources:
        text = path.read_text(errors='replace')
        registered.update(XML_FILE.findall(text))
        registered.update(WIDGET.findall(text))
        for m in CREATE.finditer(text):
            line = text.count('\n', 0, m.start(1)) + 1
            created.append((str(path.relative_to(root)), line, m.group(1)))
    return [c for c in sorted(created) if c[2] not in registered and not c[2].startswith('lv_')]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--root', default='.')
    args = ap.parse_args()

    findings = scan(Path(args.root).resolve())
    if not findings:
        print('✅ XML create names: every lv_xml_create() component in src/ can register')
        return 0
    print(f'❌ {len(findings)} lv_xml_create() call(s) name a component with no XML file:')
    for f, ln, name in findings:
        print(f'   {f}:{ln}: "{name}" (no ui_xml/{name}.xml or ui_xml/components/{name}.xml)')
    return 1


if __name__ == '__main__':
    sys.exit(main())
