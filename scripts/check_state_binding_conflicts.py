#!/usr/bin/env python3
# Copyright (C) 2025-2026 356C LLC
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Lint gate: one widget state, one binding.
#
# `<bind_state_if_eq>` and its siblings compile to lv_obj_bind_state_if_eq(),
# whose observer is unconditional in BOTH directions:
#
#   lib/lvgl/src/core/lv_observer.c  obj_state_observer_cb()
#       if(res) lv_obj_add_state(observer->target, p->flag);
#       else    lv_obj_remove_state(observer->target, p->flag);
#
# Nothing there knows about sibling bindings. Two of them on the same widget and
# the same state do not compose into an OR: each fires only when ITS subject
# changes, and asserts both polarities when it does. So whichever subject moved
# last decides the state, and a binding whose condition is false actively CLEARS
# what another binding set.
#
# The failure is silent and intermittent -- the widget is correct until some
# unrelated subject in the group happens to change. On the filament panel that
# meant Load sat enabled through an active print (the panel had computed
# load_disabled=1 and published it; a later filament_operation_in_progress 1->0
# wiped the disabled state off the button), and the click was then refused by
# AmsSubscriptionBackend::run_filament_op with nothing on screen to explain it.
#
# THE FIX
#   Collapse the group into one expression binding, which the evaluator
#   re-evaluates whenever ANY referenced subject changes:
#
#     <bind_state_if cond="a eq 1 or b ne 2 or c eq 0" state="disabled"/>
#
#   Operators are the word forms (eq ne lt le gt ge, and or not); `&&` and `<`
#   would need XML escaping. See lib/helix-xml/src/xml/lv_xml_expr.c.
#
# OPT-OUT
#   `<!-- STATE_BINDING_OK: reason -->` within 6 lines above the element, for a
#   group where only one binding can ever be live (header_bar's action_button
#   takes EITHER a subject name OR an expression from its caller, and the unused
#   one defaults to a constant that fires once at bind and never again).
#
# Usage:
#   ./scripts/check_state_binding_conflicts.py            # scan ui_xml/
#   ./scripts/check_state_binding_conflicts.py --list     # every conflicting site
#   ./scripts/check_state_binding_conflicts.py --summary  # counts only
#   ./scripts/check_state_binding_conflicts.py --max-allowed N

import argparse
import collections
import re
import subprocess
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

SCAN_DIR = Path('ui_xml')
BIND_PREFIX = 'bind_state_if'
OPT_OUT = 'STATE_BINDING_OK'
OPT_OUT_LOOKBACK = 6

# Every site is expected fixed. Raise only with a STATE_BINDING_OK reason instead.
DEFAULT_MAX_ALLOWED = 0


def parse(path):
    """LVGL state selectors (style_bg_opa:checked=) read as namespaces to any XML
    parser, so neutralise the colon before handing the text to ElementTree."""
    text = path.read_text(encoding='utf-8', errors='ignore')
    sanitised = re.sub(r'(\s[\w-]+):(\w+)=', r'\1__\2=', text)
    try:
        return ET.fromstring(sanitised), text.split('\n')
    except ET.ParseError:
        return None, None


def opted_out(lines, element_name):
    """True when a STATE_BINDING_OK comment sits just above the element, or
    anywhere inside it before the first binding -- which is where an annotation
    explaining those bindings naturally goes."""
    if not element_name:
        return False
    for i, line in enumerate(lines):
        if f'name="{element_name}"' not in line:
            continue
        lo = max(0, i - OPT_OUT_LOOKBACK)
        end = i + 1
        while end < len(lines) and f'<{BIND_PREFIX}' not in lines[end]:
            end += 1
            if end - i > 40:  # runaway: element has no binding on this path
                break
        if any(OPT_OUT in l for l in lines[lo:end + 1]):
            return True
    return False


def conflicts(path):
    root, lines = parse(path)
    if root is None:
        return []
    found = []
    for el in root.iter():
        by_state = collections.Counter(
            ch.get('state', '?') for ch in el if ch.tag.startswith(BIND_PREFIX))
        for state, count in sorted(by_state.items()):
            if count > 1 and not opted_out(lines, el.get('name')):
                found.append((path, el.get('name', '<unnamed>'), state, count))
    return found


def collect_files(args):
    if args.files:
        return [Path(f) for f in args.files]
    if args.staged_only:
        out = subprocess.run(['git', 'diff', '--cached', '--name-only', '--diff-filter=ACM'],
                             capture_output=True, text=True).stdout.split()
        return [Path(f) for f in out if f.endswith('.xml') and Path(f).exists()]
    return sorted(SCAN_DIR.rglob('*.xml'))


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('files', nargs='*', help=f'XML files to check (default: scan {SCAN_DIR}/)')
    ap.add_argument('--staged-only', action='store_true', help='Check only staged XML files')
    ap.add_argument('--list', action='store_true', help='Print every conflicting site')
    ap.add_argument('--summary', action='store_true', help='Print the count only')
    ap.add_argument('--max-allowed', type=int, default=DEFAULT_MAX_ALLOWED,
                    help=f'Ratchet ceiling (default {DEFAULT_MAX_ALLOWED})')
    args = ap.parse_args()

    found = [c for f in collect_files(args) for c in conflicts(f)]

    if args.list or (found and not args.summary):
        for path, name, state, count in found:
            print(f'{path}: <{name}> has {count} bindings on state="{state}" '
                  f'— they clobber each other; use one <bind_state_if cond="..."/>')

    if args.summary:
        print(f'{len(found)} conflicting state binding(s)')

    if len(found) > args.max_allowed:
        print(f'\nFAIL: {len(found)} conflicting state binding(s), '
              f'max allowed {args.max_allowed}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
