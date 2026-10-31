# SPDX-License-Identifier: GPL-3.0-or-later
"""
Tests for the plugin's capability probes (helix_capabilities.py): each probe
on its own, then the dispatch that combines them.

Run with: pytest tests/test_helix_capabilities.py -v
"""

import sys
import types
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).parent.parent))

from helix_capabilities import (
    NATIVE_FORMAT_PROBES,
    prints_3mf_natively,
    qidi_3mf_probe,
    valid_gcode_exts_probe,
)


def file_manager_in(name: str, source: str):
    """A FileManager instance whose class lives in a module built from
    @p source, the way Moonraker's own file_manager module holds both."""
    module = types.ModuleType(name)
    exec(compile(source, name, "exec"), module.__dict__)
    sys.modules[name] = module
    return module.FileManager()


UPSTREAM = """
VALID_GCODE_EXTS = ['.gcode', '.g', '.gco', '.ufp', '.nc']
class FileManager:
    pass
"""

NO_LIST = """
class FileManager:
    pass
"""

QIDI = """
VALID_GCODE_EXTS = ['.gcode', '.g', '.gco', '.ufp', '.nc', '.3mf']
class FileManager:
    def __init__(self):
        self.gcode_metadata = MetadataStorage()
    async def _finish_3mf_upload(self, upload_info):
        return {}
class MetadataStorage:
    def parse_3mf_thumbnail(self, path):
        return None
"""

QIDI_METHOD_ONLY = """
class FileManager:
    async def _finish_3mf_upload(self, upload_info):
        return {}
"""

# The metadata storage reached only through the instance, its class defined
# under another name (as a fork that moved it to its own module would have it).
QIDI_THUMBNAIL_ONLY = """
class FileManager:
    def __init__(self):
        self.gcode_metadata = ThreeMfAwareStorage()
class ThreeMfAwareStorage:
    def parse_3mf_thumbnail(self, path):
        return None
"""

QIDI_THUMBNAIL_CLASS_ONLY = """
class FileManager:
    pass
class MetadataStorage:
    def parse_3mf_thumbnail(self, path):
        return None
"""


class TestValidGcodeExtsProbe:
    def test_upstream_list_says_no(self):
        assert valid_gcode_exts_probe(file_manager_in("cap_upstream", UPSTREAM)) is False

    def test_a_list_with_3mf_says_yes(self):
        assert valid_gcode_exts_probe(file_manager_in("cap_qidi_a", QIDI)) is True

    def test_no_list_cannot_tell(self):
        assert valid_gcode_exts_probe(file_manager_in("cap_nolist_a", NO_LIST)) is None


class TestQidiProbe:
    @pytest.mark.parametrize("name,source", [
        ("cap_qidi_b", QIDI),
        ("cap_qidi_method", QIDI_METHOD_ONLY),
        ("cap_qidi_thumb", QIDI_THUMBNAIL_ONLY),
        ("cap_qidi_thumb_class", QIDI_THUMBNAIL_CLASS_ONLY),
    ])
    def test_recognises_qidis_3mf_code(self, name, source):
        assert qidi_3mf_probe(file_manager_in(name, source)) is True

    @pytest.mark.parametrize("name,source", [
        ("cap_upstream_b", UPSTREAM), ("cap_nolist_b", NO_LIST)])
    def test_says_nothing_about_other_moonrakers(self, name, source):
        assert qidi_3mf_probe(file_manager_in(name, source)) is None


class TestDispatch:
    def test_every_probe_is_registered(self):
        assert set(NATIVE_FORMAT_PROBES) == {qidi_3mf_probe, valid_gcode_exts_probe}

    def test_upstream_moonraker_does_not(self):
        assert prints_3mf_natively(file_manager_in("cap_upstream_c", UPSTREAM)) is False

    def test_a_vendor_yes_wins_over_a_generic_no(self):
        # The extension list is upstream's, the fork's own method is not.
        fm = file_manager_in("cap_method_upstream_list", UPSTREAM + QIDI_METHOD_ONLY)
        assert prints_3mf_natively(fm) is True

    def test_nobody_can_tell(self):
        assert prints_3mf_natively(file_manager_in("cap_nolist_c", NO_LIST)) is None

    def test_combines_any_probes_it_is_given(self):
        fm = object()
        assert prints_3mf_natively(fm, (lambda f: None, lambda f: False)) is False
        assert prints_3mf_natively(fm, (lambda f: False, lambda f: True)) is True
        assert prints_3mf_natively(fm, ()) is None
