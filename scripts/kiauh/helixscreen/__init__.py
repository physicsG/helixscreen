# SPDX-License-Identifier: GPL-3.0-or-later
from __future__ import annotations

from pathlib import Path
from typing import Optional

MODULE_PATH = Path(__file__).resolve().parent
HELIXSCREEN_REPO = "https://github.com/prestonbrown/helixscreen"
HELIXSCREEN_DIR = Path.home().joinpath("helixscreen")
HELIXSCREEN_SERVICE_NAME = "helixscreen"
HELIXSCREEN_INSTALLER_URL = (
    "https://releases.helixscreen.org/install.sh"
)

# Platform-dependent install locations
_INSTALL_PATHS = [
    Path.home().joinpath("helixscreen"),
    Path("/opt/helixscreen"),
    Path("/usr/data/helixscreen"),
    Path("/root/printer_software/helixscreen"),
]


def _has_helix_screen(path: Path) -> bool:
    # Releases ship the binary at <install>/bin/helix-screen; very old
    # pre-1.0 installs put it at the top level. Accept either so update/
    # remove don't false-negative.
    return (
        path.joinpath("bin", "helix-screen").exists()
        or path.joinpath("helix-screen").exists()
    )


def find_install_dir() -> Optional[Path]:
    """Find the actual HelixScreen install directory."""
    for path in _INSTALL_PATHS:
        try:
            if path.exists() and _has_helix_screen(path):
                return path
        except PermissionError:
            continue
    # Also scan /home/*/helixscreen
    home = Path("/home")
    if home.is_dir():
        try:
            for home_dir in home.iterdir():
                candidate = home_dir.joinpath("helixscreen")
                if candidate.exists() and _has_helix_screen(candidate):
                    return candidate
        except PermissionError:
            pass
    return None
