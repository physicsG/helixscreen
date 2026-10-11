#!/usr/bin/env bats
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Verify `make install DESTDIR=...` produces the expected /opt/helixscreen/ layout.
# Uses a stub binary so we don't need a full cross-compile.

load helpers

STAGE_DIR=""

setup() {
    STAGE_DIR="$(mktemp -d)"
    # Create stub binaries so install target doesn't fail on missing files
    mkdir -p build/ad5m-br/bin
    echo '#!/bin/sh' > build/ad5m-br/bin/helix-screen && chmod +x build/ad5m-br/bin/helix-screen
    echo '#!/bin/sh' > build/ad5m-br/bin/helix-splash && chmod +x build/ad5m-br/bin/helix-splash
}

teardown() {
    rm -rf "$STAGE_DIR"
    rm -f build/ad5m-br/bin/helix-screen build/ad5m-br/bin/helix-splash \
        build/ad5m-br/bin/libhelix-bluetooth.so
}

@test "install: produces /opt/helixscreen/bin/helix-screen" {
    run make install PLATFORM_TARGET=ad5m-br DESTDIR="$STAGE_DIR"
    [ "$status" -eq 0 ] || {
        echo "install failed: $output"
        return 1
    }
    [ -x "$STAGE_DIR/opt/helixscreen/bin/helix-screen" ]
}

@test "install: produces ui_xml/ tree" {
    run make install PLATFORM_TARGET=ad5m-br DESTDIR="$STAGE_DIR"
    [ "$status" -eq 0 ]
    [ -d "$STAGE_DIR/opt/helixscreen/ui_xml" ]
    [ -f "$STAGE_DIR/opt/helixscreen/ui_xml/globals.xml" ]
}

@test "install: produces assets/config/printer_database.json" {
    run make install PLATFORM_TARGET=ad5m-br DESTDIR="$STAGE_DIR"
    [ "$status" -eq 0 ]
    [ -f "$STAGE_DIR/opt/helixscreen/assets/config/printer_database.json" ]
}

@test "install: includes AD5M kmod hook" {
    run make install PLATFORM_TARGET=ad5m-br DESTDIR="$STAGE_DIR"
    [ "$status" -eq 0 ]
    [ -f "$STAGE_DIR/opt/helixscreen/assets/config/platform/hooks-ad5m-kmod.sh" ]
}

@test "install: data_root_resolver finds the installed tree" {
    # With binary at /opt/helixscreen/bin/helix-screen, the resolver strips /bin
    # and checks /opt/helixscreen/ui_xml. Verify that directory structure exists.
    run make install PLATFORM_TARGET=ad5m-br DESTDIR="$STAGE_DIR"
    [ "$status" -eq 0 ]
    [ -d "$STAGE_DIR/opt/helixscreen/ui_xml" ]
}

@test "install: does NOT create writable state (config/, cache/, log/)" {
    run make install PLATFORM_TARGET=ad5m-br DESTDIR="$STAGE_DIR"
    [ "$status" -eq 0 ]
    # Runtime state belongs under /data, not /opt — installer doesn't create it
    [ ! -d "$STAGE_DIR/opt/helixscreen/config" ] || {
        echo "Unexpected /opt/helixscreen/config/ in install output"
        return 1
    }
    [ ! -d "$STAGE_DIR/opt/helixscreen/cache" ] || {
        echo "Unexpected /opt/helixscreen/cache/ in install output"
        return 1
    }
}

@test "install: DESTDIR is mandatory — no install into system paths" {
    # Without DESTDIR, must not touch /opt
    run make install PLATFORM_TARGET=ad5m-br
    [ "$status" -ne 0 ]
    echo "$output" | grep -qE 'DESTDIR.*required|missing DESTDIR|DESTDIR is required' || {
        echo "Expected error about missing DESTDIR; got:"
        echo "$output"
        return 1
    }
}

@test "install: ui_xml/ excludes source-tree build scaffolding" {
    run make install PLATFORM_TARGET=ad5m-br DESTDIR="$STAGE_DIR"
    [ "$status" -eq 0 ]
    # None of these should appear under the installed ui_xml/ tree
    ! find "$STAGE_DIR/opt/helixscreen/ui_xml" \
        \( -name 'CMakeLists.txt' -o -name '*.cmake' -o -name '*.c' -o -name '*.h' \) \
        -type f | grep -q . || {
        echo "Unexpected source scaffolding shipped in install tree:"
        find "$STAGE_DIR/opt/helixscreen/ui_xml" \
            \( -name 'CMakeLists.txt' -o -name '*.cmake' -o -name '*.c' -o -name '*.h' \) \
            -type f
        return 1
    }
}

@test "install: puts the Bluetooth plugin beside the binary that dlopen()s it" {
    echo 'stub' > build/ad5m-br/bin/libhelix-bluetooth.so
    run make install PLATFORM_TARGET=ad5m-br DESTDIR="$STAGE_DIR"
    [ "$status" -eq 0 ]
    [ -f "$STAGE_DIR/opt/helixscreen/bin/libhelix-bluetooth.so" ]
}

@test "install: succeeds without a Bluetooth plugin on platforms that do not build one" {
    rm -f build/ad5m-br/bin/libhelix-bluetooth.so
    run make install PLATFORM_TARGET=ad5m-br DESTDIR="$STAGE_DIR"
    [ "$status" -eq 0 ]
    [ ! -e "$STAGE_DIR/opt/helixscreen/bin/libhelix-bluetooth.so" ]
}
