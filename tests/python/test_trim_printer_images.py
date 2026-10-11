#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for scripts/trim_printer_images.py: the crop box and the regions remap."""

import sys
from pathlib import Path

from PIL import Image

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(REPO_ROOT / "scripts"))

import trim_printer_images  # noqa: E402
from trim_printer_images import MARGIN, crop_box, remap_entry  # noqa: E402


def _art(size, content):
    im = Image.new("RGBA", size, (0, 0, 0, 0))
    im.paste((200, 50, 50, 255), content)
    return im


def test_crop_box_is_content_plus_margin():
    assert crop_box(_art((100, 80), (20, 10, 60, 70))) == \
        (20 - MARGIN, 10 - MARGIN, 60 + MARGIN, 70 + MARGIN)


def test_crop_box_ignores_near_transparent_haze():
    im = _art((100, 80), (20, 10, 60, 70))
    im.paste((0, 0, 0, 8), (0, 0, 5, 5))
    assert crop_box(im) == (20 - MARGIN, 10 - MARGIN, 60 + MARGIN, 70 + MARGIN)


def test_crop_box_is_clamped_and_idempotent():
    im = _art((100, 80), (0, 0, 60, 70))
    box = crop_box(im)
    assert box == (0, 0, 60 + MARGIN, 70 + MARGIN)
    cropped = im.crop(box)
    assert crop_box(cropped) == (0, 0, cropped.width, cropped.height)


def test_opaque_rgb_art_is_left_alone():
    assert crop_box(Image.new("RGB", (40, 30))) == (0, 0, 40, 30)


def test_remap_names_the_same_content_pixel():
    entry = {"size": [1000, 500], "nozzle": [0.5, 0.2], "bed": [[0.3, 0.9], [0.7, 0.9]]}
    box = (100, 50, 900, 450)
    out = remap_entry(entry, box)
    assert out["size"] == [800, 400]
    for old, new in [(entry["nozzle"], out["nozzle"])] + list(zip(entry["bed"], out["bed"])):
        assert abs(old[0] * 1000 - (new[0] * 800 + box[0])) <= 1
        assert abs(old[1] * 500 - (new[1] * 400 + box[1])) <= 1


def _run(monkeypatch, tmp_path, *argv):
    monkeypatch.setattr(trim_printer_images, "IMAGES_DIR", tmp_path)
    monkeypatch.setattr(trim_printer_images, "REGIONS", tmp_path / "regions.json")
    monkeypatch.setattr(sys, "argv", ["trim_printer_images.py", *argv])
    return trim_printer_images.main()


def test_check_fails_on_margin_then_passes_after_trim(monkeypatch, tmp_path):
    _art((100, 80), (20, 10, 60, 70)).save(tmp_path / "p.png")
    (tmp_path / "regions.json").write_text(
        '{"p": {"size": [100, 80], "nozzle": [0.4, 0.5], "bed": [[0.2, 0.875], [0.6, 0.875]]}}')
    assert _run(monkeypatch, tmp_path, "--check") == 1
    assert _run(monkeypatch, tmp_path) == 0
    assert _run(monkeypatch, tmp_path, "--check") == 0
    assert Image.open(tmp_path / "p.png").size == (44, 64)
    regions = trim_printer_images.json.loads((tmp_path / "regions.json").read_text())
    assert regions["p"]["size"] == [44, 64]
    assert regions["p"]["nozzle"] == [round((40 - 18) / 44, 3), round((40 - 8) / 64, 3)]
