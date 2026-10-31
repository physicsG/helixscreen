#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Which ui_xml/translations/ files the app never loads at runtime.

The runtime registers one <lang>.xml per locale (src/system/translation_loader.cpp)
and skips the identity locale, whose pack maps every tag to itself. The merged
all-languages catalogue is a build artefact no runtime path reads. Every packager
(release tarballs via platform_manifest.py, the ESP32 image via
esp32_stage_assets.py, the APK via android/app/build.gradle) leaves these out.

The identity locale is read from kIdentityLocale in include/translation_loader.h,
so the packagers and the loader cannot disagree about which pack is skipped.
"""

import os
import re

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LOADER_HEADER = os.path.join(REPO_ROOT, "include", "translation_loader.h")

# Written by scripts/generate_translations.py; TRANS_XML in mk/translations.mk.
MERGED_CATALOGUE = "translations.xml"


def _identity_locale(header=LOADER_HEADER):
    with open(header, encoding="utf-8") as f:
        m = re.search(r'kIdentityLocale\s*=\s*"([^"]+)"', f.read())
    if not m:
        raise RuntimeError(f"kIdentityLocale not found in {header}")
    return m.group(1)


IDENTITY_LOCALE = _identity_locale()

# File names under ui_xml/translations/ that no runtime path loads.
UNLOADED_TRANSLATION_FILES = (MERGED_CATALOGUE, f"{IDENTITY_LOCALE}.xml")
