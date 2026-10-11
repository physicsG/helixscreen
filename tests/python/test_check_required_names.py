# Copyright (C) 2025-2026 356C LLC
# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for scripts/check_required_names.py: comment stripping and extends= expansion."""

import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "scripts"))

from check_required_names import XmlTree, call_args, strip_comments  # noqa: E402


def test_slashes_inside_a_string_are_not_a_comment():
    src = 'f({"x", "https://a"}, 1); // tail\ng();'
    out = strip_comments(src)
    assert '"https://a"}, 1);' in out
    assert "tail" not in out
    args, _ = call_args(out, out.index("("))
    assert args == ['{"x", "https://a"}', "1"]


def test_block_comment_keeps_line_numbers():
    out = strip_comments("a /* x\ny */ b")
    assert out.count("\n") == 1 and "x" not in out


def test_view_extends_picks_up_the_wrapper_names(tmp_path):
    ui = tmp_path / "ui_xml"
    ui.mkdir()
    (ui / "wrapper.xml").write_text(
        '<component><view name="wrapper"><lv_obj name="overlay_header"/></view></component>'
    )
    (ui / "page.xml").write_text(
        '<component><view name="page" extends="wrapper"><lv_obj name="body"/></view></component>'
    )
    names = XmlTree(tmp_path, {}).names("page", [])
    assert {"body", "overlay_header"} <= names
