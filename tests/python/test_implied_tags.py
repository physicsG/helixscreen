"""The extractor's implied-tag rule must agree with the XML engine's.

lib/helix-xml/src/xml/lv_xml.c#collect_implied_tags gives an element its
implied translation tags at runtime; scripts/translations/extractor.py
#implied_tags decides the same thing for extraction and the redundant-tag lint.
The engine rule is modelled here from an ElementTree parse, independently of the
extractor's regex scan, and the two must agree on every element in ui_xml.
helix::ui::is_translation_key() is the engine's key callback; it and
should_skip_text() must agree on tests/fixtures/translation_key_cases.json,
which tests/unit/test_translation_key_filter.cpp checks from the C++ side.
"""

import json
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "scripts"))

from translations.extractor import (  # noqa: E402
    _decode_xml_entities,
    _blank_xml_comments,
    component_tag_props,
    implied_tags,
    iter_elements,
    should_skip_text,
)

UI_XML = REPO_ROOT / "ui_xml"
NEVER_IMPLIED = ("translation_tag", "options_tag", "placeholder_tag")


def _ui_xml_files():
    return [f for f in sorted(UI_XML.rglob("*.xml")) if "translations" not in f.parts]


def _engine_props():
    props = {}
    for f in _ui_xml_files():
        root = ET.parse(f).getroot()
        api = root.find("api") if root.tag == "component" else None
        if api is None:
            continue
        names = {p.get("name") for p in api.iter("prop")}
        tags = {n for n in names if n and n.endswith("_tag") and len(n) > 4}
        if tags:
            props.setdefault(f.stem, set()).update(tags)
    return props


def _engine_value(attrs, tag):
    if tag in attrs or "bind_text" in attrs:
        return None
    if tag == "translation_tag":
        value = attrs.get("text")
    else:
        value = attrs.get(tag[:-4])
        if value is None:
            value = attrs.get(tag[:-4] + "_text")
    if not value or value[0] in "$#" or "${" in value:
        return None
    return None if should_skip_text(value) else value


def _engine_implied(element, attrs, props):
    name = attrs.get("extends", "lv_obj") if element == "view" else element
    out = {}
    for tag in ["translation_tag"] + sorted(props.get(name, ())):
        if tag != "translation_tag" and tag in NEVER_IMPLIED:
            continue
        value = _engine_value(attrs, tag)
        if value is not None:
            out[tag] = value
    return out


def test_fixture_verdicts_match_should_skip_text():
    data = json.loads((REPO_ROOT / "tests/fixtures/translation_key_cases.json").read_text())
    wrong = [text for text, is_key in data["cases"] if should_skip_text(text) == is_key]
    assert len(data["cases"]) > 1000
    assert not wrong, f"should_skip_text disagrees with the fixture on: {wrong[:20]}"


def test_extractor_props_match_the_parsed_api():
    parsed = {k: v for k, v in _engine_props().items() if v}
    assert component_tag_props(UI_XML) == parsed


def test_extractor_implies_what_the_engine_implies():
    props = _engine_props()
    extractor_props = component_tag_props(UI_XML)
    drift = []
    for f in _ui_xml_files():
        tree = list(ET.parse(f).getroot().iter())
        scanned = list(iter_elements(f.read_text(encoding="utf-8")))
        assert len(tree) == len(scanned), f"{f}: the regex scan sees a different element count"
        for node, (element, attrs, _m) in zip(tree, scanned):
            assert node.tag == element, f"{f}: element order differs at <{node.tag}>"
            got = {
                t: _decode_xml_entities(v)
                for t, v in implied_tags(element, attrs, extractor_props).items()
            }
            want = _engine_implied(node.tag, node.attrib, props)
            if got != want:
                drift.append(f"{f.relative_to(REPO_ROOT)} <{element}>: {got} != {want}")
    assert not drift, "extractor and engine disagree:\n  " + "\n  ".join(drift[:20])


def test_no_single_quoted_attributes():
    """The extractor's element scan reads double-quoted values only."""
    pattern = re.compile(r"<[A-Za-z_][^<>]*?\s[\w:.-]+\s*=\s*'")
    found = [
        str(f.relative_to(REPO_ROOT))
        for f in _ui_xml_files()
        if pattern.search(_blank_xml_comments(f.read_text(encoding="utf-8")))
    ]
    assert not found, f"single-quoted attributes; use double quotes: {found}"


def test_placeholder_and_options_tags_are_never_implied():
    props = {"field": {"placeholder_tag", "options_tag", "label_tag"}}
    attrs = {"placeholder": "Name", "options": "Auto", "label": "Fan"}
    assert implied_tags("field", attrs, props) == {"label_tag": "Fan"}


def test_a_commented_out_prop_declares_nothing(tmp_path):
    ui = tmp_path / "ui_xml"
    ui.mkdir()
    (ui / "row.xml").write_text(
        '<component><api><!-- <prop name="old_tag" type="string"/> -->'
        '<prop name="label_tag" type="string"/></api><view/></component>'
    )
    assert component_tag_props(ui) == {"row": {"label_tag"}}
