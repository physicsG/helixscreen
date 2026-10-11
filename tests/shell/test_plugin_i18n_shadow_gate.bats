#!/usr/bin/env bats
# SPDX-License-Identifier: GPL-3.0-or-later

# The plugin-i18n-shadow gate's two halves: it fires on a pack entry whose
# tag matches a system string (that entry would override the app's own
# translation app-wide while the plugin is loaded), and it stays silent on
# plugin-only strings the catalog does not carry.

setup() {
    load helpers
    GATE="scripts/check_plugin_i18n_shadow.py"
}

@test "plugin i18n shadow: fires on a tag the app catalog already has" {
    tmp=$(mktemp -d)
    mkdir -p "$tmp/i18n"
    cat > "$tmp/i18n/de.xml" <<'EOF'
<?xml version="1.0" encoding="UTF-8"?>
<translations languages="de">
  <translation tag="Start" de="Starten"/>
  <translation tag="a string only this plugin says" de="Nur dieses Plugin"/>
</translations>
EOF
    run python3 "$GATE" --root "$tmp"
    [ "$status" -ne 0 ]
    grep -q "shadows the app catalog" <<<"$output"
    grep -q "'Start'" <<<"$output"
    rm -rf "$tmp"
}

@test "plugin i18n shadow: silent on plugin-only strings and missing languages" {
    tmp=$(mktemp -d)
    mkdir -p "$tmp/i18n"
    cat > "$tmp/i18n/de.xml" <<'EOF'
<?xml version="1.0" encoding="UTF-8"?>
<translations languages="de">
  <translation tag="a string only this plugin says" de="Nur dieses Plugin"/>
</translations>
EOF
    run python3 "$GATE" --root "$tmp"
    [ "$status" -eq 0 ]
    rm -rf "$tmp"
}

@test "plugin i18n shadow: the repository's example and fixture packs are clean" {
    run python3 "$GATE"
    [ "$status" -eq 0 ]
}
