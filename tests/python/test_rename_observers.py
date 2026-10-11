# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for scripts/rename_observers.py: the rewrite is exact and idempotent."""

import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "scripts"))

from rename_observers import rewrite_code, rewrite_usings  # noqa: E402


def rename(text):
    return rewrite_code(rewrite_usings(text))


def test_deferred_forms_drop_the_owner_template_arg():
    src = "g = observe_int_sync<Panel>(s, this, h, lt);\ng = helix::ui::observe_string<Map<int, X>>(s, p, h, lt);\n"
    assert rename(src) == (
        "g = observe<int>(s, this, h, lt);\ng = helix::ui::observe<const char*>(s, p, h, lt);\n"
    )


def test_immediate_forms_gain_dispatch_after_the_last_argument():
    src = "g = observe_int_immediate<P>(s, this, [](P*, int) { f(\")\"); }, lt);\n"
    assert rename(src) == (
        'g = observe<int>(s, this, [](P*, int) { f(")"); }, lt, Dispatch::Immediate);\n'
    )
    assert rename("x = ui::observe_print_state_immediate<P>(s, p, h, lt);") == (
        "x = ui::observe_print_state(s, p, h, lt, ui::Dispatch::Immediate);"
    )


def test_using_declarations_collapse_at_file_scope_only():
    src = (
        "using helix::ui::observe_int_sync;\nusing helix::ui::observe_string;\n"
        "void f() {\n    using helix::ui::observe_int_sync;\n}\n"
    )
    assert rename(src) == (
        "using helix::ui::observe;\nvoid f() {\n    using helix::ui::observe;\n}\n"
    )
    assert "using helix::ui::Dispatch;" in rename("using helix::ui::observe_int_immediate;\n")


def test_rewrite_is_idempotent_and_leaves_new_api_alone():
    src = (
        "g = observe_int_sync<P>(s, this, h, lt);\nobserve_string_immediate<P>(s, p, h, lt);\n"
        "h = observe<int>(s, this, k, lt);\nauto observe_string_view = 1;\n"
    )
    once = rename(src)
    assert rename(once) == once
    assert "observe_string_view" in once
