# SPDX-License-Identifier: GPL-3.0-or-later
"""App-startup wiring of the stock detection sources (#1378).

Application is not linked into the unit-test binary (app_globals.o is
filtered out of TEST_APP_OBJS), so the registration block in
Application::init_panel_subjects() is only observable in a real boot. This
starts one at debug level and asserts the K2 source announced itself: the
"not capable" line on a non-K2 host, the capable one on a K2.
"""
import os
from pathlib import Path

import pytest

from helix.app import HelixApp

_REPO_ROOT = Path(__file__).resolve().parents[2]
_BINARY = Path(os.environ.get(
    "HELIX_UI_BINARY", str(_REPO_ROOT / "build" / "bin" / "helix-screen")))


def test_k2_stock_source_registered_at_boot(tmp_path):
    if not _BINARY.exists():
        pytest.skip(f"{_BINARY} not built - run `make -j`")

    app = HelixApp(binary=_BINARY, socket_path=tmp_path / "control.sock",
                   log_path=tmp_path / "app.log", extra_args=["-vv"])
    with app:
        boot_log = (tmp_path / "app.log").read_text()

    assert "[K2StockSource]" in boot_log, (
        "K2 stock detection source never started - registration in "
        "Application::init_panel_subjects() is missing")
