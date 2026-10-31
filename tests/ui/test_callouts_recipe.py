# SPDX-License-Identifier: GPL-3.0-or-later

"""The callouts-4x2 screenshot recipe widens the printer image on the default home.

The recipe frees the cells beside the image and resizes it through ctl, so it
must hold for whichever widgets the capability-resolved default places there.
This boots the default 800x480 mock, runs the recipe exactly as
screenshot.sh would, and reads the image's geometry back.
"""

from __future__ import annotations

import os
import shlex
import subprocess
from pathlib import Path

import pytest

from helix.app import HelixApp

REPO_ROOT = Path(__file__).resolve().parents[2]
RECIPES_SCRIPT = REPO_ROOT / "scripts" / "screenshot-recipes.sh"
BINARY = Path(os.environ.get("HELIX_UI_BINARY", str(REPO_ROOT / "build" / "bin" / "helix-screen")))


def _recipe(token: str) -> str:
    script = (f"source {shlex.quote(str(RECIPES_SCRIPT))}; "
              f"screenshot_recipe_for {shlex.quote(token)}")
    result = subprocess.run(["bash", "-c", script], capture_output=True, text=True,
                            check=True, cwd=REPO_ROOT)
    recipe = result.stdout.strip()
    assert recipe, f"{token}: no recipe in {RECIPES_SCRIPT}"
    return recipe


def _image_cells(app: HelixApp) -> dict:
    """printer_image's placement on the first home page, in cells."""
    pages = app.ctl("home")["pages"]
    placed = [w for w in pages[0]["widgets"] if w["id"] == "printer_image"]
    assert placed, "printer_image is not placed on the first home page"
    return placed[0]


@pytest.fixture
def app(tmp_path):
    if not BINARY.exists():
        pytest.skip(f"{BINARY} not built - run `make -j`")
    instance = HelixApp(binary=BINARY,
                        socket_path=tmp_path / "control.sock",
                        log_path=tmp_path / "app.log")
    with instance:
        yield instance


def test_callouts_4x2_spans_the_image_four_cells_by_two(app):
    app.wait_for("nav_buttons_enabled", 1, timeout=30)
    app.navigate("home")
    app.wait_idle()
    before = _image_cells(app)
    assert (before["w"], before["h"]) == (2, 2)

    for clause in _recipe("callouts-4x2").split(";"):
        parts = clause.split()
        if parts:
            app.ctl(*parts)
    app.wait_idle()
    after = _image_cells(app)

    assert (after["col"], after["row"]) == (before["col"], before["row"])
    assert (after["w"], after["h"]) == (4, 2)
