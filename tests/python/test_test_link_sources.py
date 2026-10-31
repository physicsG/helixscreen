# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for scripts/test_link_sources.py: the files the helix-tests stale stamp covers.

The stamp must move only for a file the binary is compiled from. A source the test
link leaves out (src/main.cpp) changing would otherwise warn that the binary is
stale forever, since `make test` never relinks for it.
"""

import os
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "scripts"))

from test_link_sources import link_sources  # noqa: E402


def _tree(tmp_path: Path) -> Path:
    for rel in [
        "src/foo.cpp",
        "src/main.cpp",
        "include/foo.h",
        "include/only_main.h",
        "tests/unit/test_new.cpp",
        "tests/test_main.cpp",
        "lib/x/x.h",
    ]:
        (tmp_path / rel).parent.mkdir(parents=True, exist_ok=True)
        (tmp_path / rel).write_text("//\n")
    obj = tmp_path / "build/obj"
    (obj / "tests").mkdir(parents=True)
    (obj / "foo.d").write_text(
        "build/obj/foo.o: src/foo.cpp include/foo.h \\\n"
        f"  {tmp_path}/lib/x/x.h\ninclude/foo.h:\n"
    )
    (obj / "main.d").write_text("build/obj/main.o: src/main.cpp include/only_main.h\n")
    return tmp_path


def test_lists_what_linked_objects_depend_on_and_nothing_else(tmp_path, monkeypatch):
    top = _tree(tmp_path)
    monkeypatch.chdir(top)
    paths = link_sources(["build/obj/foo.o", "build/obj/tests/test_new.o"], "build/obj", os.getcwd())
    assert paths == ["include/foo.h", "src/foo.cpp", "tests/unit/test_new.cpp"]


def test_an_app_only_source_is_not_covered(tmp_path, monkeypatch):
    top = _tree(tmp_path)
    monkeypatch.chdir(top)
    paths = link_sources(["build/obj/foo.o"], "build/obj", os.getcwd())
    assert "src/main.cpp" not in paths
    assert "include/only_main.h" not in paths


def test_an_object_without_a_dep_file_names_its_own_source(tmp_path, monkeypatch):
    top = _tree(tmp_path)
    monkeypatch.chdir(top)
    paths = link_sources(["build/obj/tests/test_main.o", "build/obj/lib.a"], "build/obj", os.getcwd())
    assert paths == ["tests/test_main.cpp"]
