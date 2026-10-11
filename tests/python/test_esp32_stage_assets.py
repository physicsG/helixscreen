#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""
Tests for the ESP32 LittleFS staging script's XML minifier: comments must be
stripped, inter-tag whitespace collapsed, and text content / attribute values
byte-preserved. Also round-trips a real ui_xml/ file through xml.etree to
confirm the minified output still parses and carries identical text/attrs.
"""

import sys
import xml.etree.ElementTree as ET
from pathlib import Path

import pytest

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(REPO_ROOT / "scripts"))

import esp32_printer_images  # noqa: E402
from esp32_stage_assets import (minify_xml, stage_config, stage_printer_images,  # noqa: E402
                                stage_translations)


def test_strips_single_line_comment():
    xml = '<component><!-- a comment --><widget/></component>'
    out = minify_xml(xml)
    assert "<!--" not in out
    assert "comment" not in out


def test_strips_multiline_comment():
    xml = "<component>\n  <!-- line one\n       line two -->\n  <widget/>\n</component>"
    out = minify_xml(xml)
    assert "<!--" not in out
    assert "line one" not in out
    assert "line two" not in out


def test_collapses_inter_tag_whitespace():
    xml = "<component>\n  <widget/>\n  <widget/>\n</component>"
    out = minify_xml(xml)
    assert ">\n" not in out
    assert "\n<" not in out
    assert out == "<component><widget/><widget/></component>"


def test_preserves_text_content_exactly():
    xml = '<label>Hello   World\n  with   odd  spacing</label>'
    out = minify_xml(xml)
    assert "Hello   World\n  with   odd  spacing" in out


def test_preserves_attribute_values_exactly():
    xml = '<widget style_pad_all="  12  " text="a    b">\n  <child/>\n</widget>'
    out = minify_xml(xml)
    assert 'style_pad_all="  12  "' in out
    assert 'text="a    b"' in out


def test_preserves_leading_trailing_whitespace_in_text_node():
    # Regression guard: text nodes sit in the same '>...<' position the
    # inter-tag collapse targets. A naive whitespace-only-blind collapse
    # would eat this text; the minifier must leave any non-whitespace-only
    # span alone.
    xml = "<a>  padded text  </a>"
    out = minify_xml(xml)
    assert out == xml


def test_does_not_touch_pure_whitespace_text_node_between_tags():
    # A genuinely empty/whitespace-only element body between two tags is
    # exactly what "inter-tag whitespace" collapse targets, distinct from
    # real (non-whitespace) text content.
    xml = "<a>\n   \n</a>"
    out = minify_xml(xml)
    assert out == "<a></a>"


def test_literal_gt_in_text_with_trailing_whitespace_not_corrupted():
    # Regression guard for the tag-boundary-unaware regex bug: a literal '>'
    # is legal unescaped in XML text content. A blind '>\s+<' regex reads the
    # literal '>' plus the trailing spaces plus the next tag's '<' as one
    # "whitespace between tags" span and silently eats the trailing spaces.
    # Compare exactly (no .strip()) — that's the whole point of the test.
    xml = "<label>value >   </label>"
    out = minify_xml(xml)
    assert out == xml


def test_literal_gt_in_attribute_value_not_corrupted():
    xml = '<widget text="a > b   ">\n  <child/>\n</widget>'
    out = minify_xml(xml)
    assert 'text="a > b   "' in out
    # the genuine inter-tag whitespace between the tags is still collapsed
    assert out == '<widget text="a > b   "><child/></widget>'


def test_globals_xml_round_trips_and_preserves_content():
    src_path = REPO_ROOT / "ui_xml" / "globals.xml"
    original = src_path.read_text(encoding="utf-8")
    minified = minify_xml(original)

    assert len(minified) < len(original), "minifier should shrink a real, comment-heavy file"
    assert "<!--" not in minified

    original_root = ET.fromstring(original)
    minified_root = ET.fromstring(minified)  # must still parse

    def normalize(value):
        # Whitespace-only (or absent) text/tail nodes are collapsed by the
        # minifier by design, so treat those as equivalent to None. Any
        # NON-empty text/tail must match byte-exactly — that's what would
        # catch tag-boundary corruption (e.g. a literal '>' in text losing
        # its trailing whitespace).
        if value is None or value.strip() == "":
            return None
        return value

    def collect(elem):
        return [
            (elem.tag, dict(elem.attrib), normalize(elem.text), normalize(elem.tail))
            for elem in elem.iter()
        ]

    assert collect(original_root) == collect(minified_root)


def test_stage_translations_ships_every_language_but_the_identity_en(tmp_path):
    translations_dir = tmp_path / "ui_xml" / "translations"
    translations_dir.mkdir(parents=True)
    for lang in ("en", "fr", "de", "ja", "zh"):
        (translations_dir / f"{lang}.xml").write_text(
            f"<translations languages=\"{lang}\"><translation tag=\"a\" {lang}=\"x\"/></translations>",
            encoding="utf-8")
    (translations_dir / "translations.xml").write_text("<translations/>", encoding="utf-8")

    _, included = stage_translations(tmp_path / "ui_xml", tmp_path / "out")

    staged = tmp_path / "out" / "ui_xml" / "translations"
    assert included == ["de", "fr"]
    assert sorted(p.name for p in staged.iterdir()) == ["de.xml", "fr.xml"]


def test_stage_translations_ships_cjk_only_when_asked(tmp_path):
    translations_dir = tmp_path / "ui_xml" / "translations"
    translations_dir.mkdir(parents=True)
    for lang in ("en", "fr", "ja", "zh"):
        (translations_dir / f"{lang}.xml").write_text(
            f"<translations languages=\"{lang}\"><translation tag=\"a\" {lang}=\"x\"/></translations>",
            encoding="utf-8")

    _, included = stage_translations(tmp_path / "ui_xml", tmp_path / "out", with_cjk=True)
    assert included == ["fr", "ja", "zh"]


def test_stage_config_ships_the_default_print_start_profile(tmp_path):
    profiles = tmp_path / "assets" / "config" / "print_start_profiles"
    profiles.mkdir(parents=True)
    (profiles / "default.json").write_text('{"name": "Generic"}', encoding="utf-8")
    (profiles / "forge_x.json").write_text('{"name": "Forge-X"}', encoding="utf-8")

    stage_config(tmp_path / "assets", tmp_path / "out")

    staged = tmp_path / "out" / "assets" / "config" / "print_start_profiles"
    assert sorted(p.name for p in staged.iterdir()) == ["default.json"]


def test_stage_config_ships_printer_presets(tmp_path):
    presets = tmp_path / "assets" / "config" / "presets"
    presets.mkdir(parents=True)
    (presets / "ad5m_pro_forgex.json").write_text('{"printer": {}}', encoding="utf-8")
    (presets / "README.md").write_text("docs", encoding="utf-8")

    stage_config(tmp_path / "assets", tmp_path / "out")

    staged = tmp_path / "out" / "assets" / "config" / "presets"
    assert sorted(p.name for p in staged.iterdir()) == ["ad5m_pro_forgex.json"]


def test_stage_printer_images_generates_absent_renditions(tmp_path):
    pytest.importorskip("PIL")
    renditions = tmp_path / "renditions"

    stage_printer_images(tmp_path / "out", renditions)

    staged = tmp_path / "out" / "assets" / "images" / "printers"
    expected = {f"{name}.png" for name in esp32_printer_images.ESP32_PRINTERS}
    assert expected <= {p.name for p in staged.iterdir()}
    assert esp32_printer_images.is_fresh(renditions)


def test_stage_printer_images_reuses_fresh_renditions(tmp_path, monkeypatch):
    renditions = tmp_path / "renditions"
    renditions.mkdir()
    for name in esp32_printer_images.ESP32_PRINTERS:
        (renditions / f"{name}.png").write_bytes(b"png")
    monkeypatch.setattr(esp32_printer_images, "generate",
                        lambda out: pytest.fail("fresh renditions were regenerated"))

    stage_printer_images(tmp_path / "out", renditions)

    assert (tmp_path / "out" / "assets" / "images" / "printers" / "generic-corexy.png").read_bytes() == b"png"


def test_stage_printer_images_regenerates_when_a_rendition_is_missing(tmp_path, monkeypatch):
    renditions = tmp_path / "renditions"
    renditions.mkdir()
    (renditions / "generic-corexy.png").write_bytes(b"png")
    calls = []
    monkeypatch.setattr(esp32_printer_images, "generate", lambda out: calls.append(out) or 1)

    with pytest.raises(SystemExit):
        stage_printer_images(tmp_path / "out", renditions)
    assert calls == [renditions]


def test_stage_printer_images_fails_without_pillow(tmp_path, monkeypatch):
    monkeypatch.setitem(sys.modules, "PIL", None)

    with pytest.raises(SystemExit) as exc:
        stage_printer_images(tmp_path / "out", tmp_path / "renditions")

    assert exc.value.code not in (0, None)
    assert not (tmp_path / "out" / "assets" / "images" / "printers").exists()
