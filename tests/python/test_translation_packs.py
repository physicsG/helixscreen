#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""The translation files no locale loads, as every packager sees them.

scripts/translation_packs.py is the single definition. The release prune and the
ESP32 stager import it; the APK's Gradle copy cannot, so its list is checked here.
"""

import re
import sys
from pathlib import Path

import pytest

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(REPO_ROOT / "scripts"))

import translation_packs  # noqa: E402


def test_identity_pack_follows_the_loader_constant(tmp_path):
    header = tmp_path / "translation_loader.h"
    header.write_text('inline constexpr const char* kIdentityLocale = "de";\n', encoding="utf-8")
    assert translation_packs._identity_locale(str(header)) == "de"


def test_a_header_without_the_constant_is_an_error(tmp_path):
    header = tmp_path / "translation_loader.h"
    header.write_text("// nothing here\n", encoding="utf-8")
    with pytest.raises(RuntimeError):
        translation_packs._identity_locale(str(header))


def test_unloaded_files_are_the_catalogue_and_the_identity_pack():
    assert translation_packs.UNLOADED_TRANSLATION_FILES == (
        "translations.xml", f"{translation_packs.IDENTITY_LOCALE}.xml")


def test_merged_catalogue_is_the_file_the_build_writes():
    mk = (REPO_ROOT / "mk" / "translations.mk").read_text(encoding="utf-8")
    m = re.search(r"^TRANS_XML\s*:=\s*(\S+)", mk, re.M)
    assert m, "TRANS_XML not found in mk/translations.mk"
    assert Path(m.group(1)).name == translation_packs.MERGED_CATALOGUE


def test_apk_excludes_exactly_the_shared_list():
    gradle = (REPO_ROOT / "android" / "app" / "build.gradle").read_text(encoding="utf-8")
    m = re.search(r"def unloadedTranslationFiles = \[([^\]]*)\]", gradle)
    assert m, "unloadedTranslationFiles not found in android/app/build.gradle"
    listed = tuple(re.findall(r"'([^']+)'", m.group(1)))
    assert sorted(listed) == sorted(translation_packs.UNLOADED_TRANSLATION_FILES)
    assert re.search(r'exclude unloadedTranslationFiles\.collect \{ "translations/\$\{it\}" \}',
                     gradle), "copyAssets no longer applies the exclusion"
