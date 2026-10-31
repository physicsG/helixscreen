#!/usr/bin/env bats
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The Android build compiles through Gradle/CMake and never reads the Makefile.
# Every feature gate's default lives in include/helix_features.h, which
# lvgl_pch.h brings into every C++ source, so the Android CMake lists only the
# gates it changes. An undefined gate in `#if` is 0 and compiles the feature out
# with no error, so the header must reach Android, and a feature the app keeps
# must default on and not be turned off here.

CMAKE="android/app/jni/CMakeLists.txt"
FEATURES="include/helix_features.h"

keeps_gate() {
  grep -qE "^#define $1 1$" "$FEATURES" || { echo "$1 does not default to 1 in $FEATURES"; return 1; }
  ! grep -qE "^[[:space:]]+$1=0\b" "$CMAKE" || { echo "$CMAKE turns $1 off"; return 1; }
}

@test "the Android build force-includes the header that holds every gate default" {
  sed -n '/^target_precompile_headers(main PRIVATE$/,/^)/p' "$CMAKE" | grep -qE '/include/lvgl_pch\.h>"$'
  grep -qE '^#include "helix_features\.h"$' include/lvgl_pch.h
}

@test "the Android app keeps every network-reachable vendor filament system" {
  for g in HELIX_HAS_CFS HELIX_HAS_IFS HELIX_HAS_ACE HELIX_HAS_QIDI HELIX_HAS_SNAPMAKER; do
    keeps_gate "$g"
  done
}

@test "the Android app keeps label printing" {
  keeps_gate HELIX_HAS_LABEL_PRINTER
}

@test "the Android app builds the screensavers" {
  grep -qE "^[[:space:]]+HELIX_ENABLE_SCREENSAVER$" "$CMAKE"
}

@test "every non-Debug Android config is a release build, on every target" {
  # lv_conf.h derives LV_USE_ASSERT_STYLE from HELIX_RELEASE_BUILD, and that
  # assert adds a member to lv_style_t, so every target including lv_conf.h must
  # agree: one directory-scope definition, ahead of the first target, and no other.
  def=$(grep -nE '^add_compile_definitions\(\$<\$<NOT:\$<CONFIG:Debug>>:HELIX_RELEASE_BUILD>\)$' "$CMAKE" | cut -d: -f1)
  [ -n "$def" ]
  first_target=$(grep -nE '^(add_library|add_subdirectory|ExternalProject_Add)\(' "$CMAKE" | head -1 | cut -d: -f1)
  [ "$def" -lt "$first_target" ]
  [ "$(grep -c 'HELIX_RELEASE_BUILD' "$CMAKE")" -eq "$(grep -cE '^add_compile_definitions\(.*HELIX_RELEASE_BUILD' "$CMAKE")" ]
}

@test "the Android app builds the Lua plugin runtime from the Makefile's Lua sources" {
  keeps_gate HELIX_HAS_PLUGINS
  grep -qE '^add_library\(lua STATIC \$\{LUA_SOURCES\}\)' "$CMAKE"
  grep -qE '^set_source_files_properties\(\$\{LUA_SOURCES\} PROPERTIES LANGUAGE CXX\)' "$CMAKE"
  # main links the interpreter
  sed -n '/^target_link_libraries(main$/,/^)/p' "$CMAKE" | grep -qE '^[[:space:]]+lua$'
  # Same exclusions as the Makefile, so the io/os/package/debug libraries stay out.
  make_excl=$(sed -nE 's/^LUA_EXCLUDED := //p' Makefile | tr ' ' '\n' | sort)
  cmake_excl=$(sed -nE 's/^foreach\(_excluded (.*)\)$/\1/p' "$CMAKE" | tr ' ' '\n' | sort)
  [ -n "$make_excl" ]
  [ "$make_excl" = "$cmake_excl" ]
}

@test "the APK leaves out the same mock-only asset trees as the Linux packages" {
  # Two hand-kept lists of one rule: mk/cross.mk RELEASE_ASSETS_EXCLUDE and
  # build.gradle mockOnlyAssets (which both copyAssets and MANIFEST.txt honour).
  make_excl=$(sed -nE 's/^RELEASE_ASSETS_EXCLUDE := //p' mk/cross.mk | tr ' ' '\n' | sed 's|^assets/||' | sort)
  gradle_excl=$(sed -nE "s/^def mockOnlyAssets = \[(.*)\]$/\1/p" android/app/build.gradle | tr -d "' " | tr ',' '\n' | sort)
  [ -n "$make_excl" ]
  [ "$make_excl" = "$gradle_excl" ]
}

@test "the APK leaves out every compiled font source" {
  # The fonts/*.c arrays are linked into libmain.so through CMake's FONT_SOURCES
  # glob; the glob exclusion in copyAssets keeps any new one out of the APK too.
  grep -qE '^file\(GLOB FONT_SOURCES "\$\{PROJECT_ROOT\}/assets/fonts/\*\.c"\)$' "$CMAKE"
  grep -qE "^def compiledFontSources = 'fonts/\*\.c'$" android/app/build.gradle
  sed -n "/^task copyAssets/,/^}/p" android/app/build.gradle | grep -qE '^[[:space:]]+exclude compiledFontSources$'
  # Nothing reads a font C source at runtime: the only runtime font loader takes
  # the binary faces under fonts/cjk/.
  ! grep -rnE '"[^"]*fonts/[^"]*\.c"' src include
}
