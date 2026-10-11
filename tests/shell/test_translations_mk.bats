#!/usr/bin/env bats
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Tests for the translations.xml rule in mk/translations.mk, driven as the
# real make rule via command-line variable overrides, so the fixture needs
# neither the tree's venv nor its YAML masters.
#
# generate_translations.py writes the master translations.xml before the
# per-locale packs, so a run that dies between the two leaves a fresh-mtime
# master over a partial set: that build fails, the next one sees the target
# up to date and ships the partial packs. Two layers keep that from
# happening: the top-level Makefile's .DELETE_ON_ERROR drops a target whose
# recipe failed after modifying it, and the rule itself removes the master
# on failure, so it stays correct from any makefile that does not set that
# flag. The first test pins the end-to-end property either layer provides;
# the last pins the rule-local half specifically.

load helpers

setup() {
    cd "$BATS_TEST_DIRNAME/../.." || return 1

    T="${BATS_TEST_TMPDIR:-$(mktemp -d)}/tr"
    mkdir -p "$T"

    # Stand-in venv python: answers the recipe's "import yaml" probe, and
    # otherwise acts as the generator. The recipe's venv branch is the only
    # one exercised, whatever python the host happens to have.
    cat > "$T/py" <<'EOF'
#!/bin/sh
if [ "$1" = "-c" ]; then
    exit 0
fi
: > "$MASTER"
if [ -n "$GEN_DIES" ]; then
    exit 1
fi
: > "$MASTER_PACK"
exit 0
EOF
    chmod +x "$T/py"

    : > "$T/gen.py"
    : > "$T/master.yml"

    export MASTER="$T/translations.xml"
    export MASTER_PACK="$T/de.xml"
    unset GEN_DIES
}

run_rule() {
    make -s \
        TRANS_XML="$T/translations.xml" \
        TRANS_SCRIPT="$T/gen.py" \
        TRANS_YAML="$T/master.yml" \
        TRANS_GEN_DIR="$T/gen" \
        VENV_PYTHON_TRANS="$T/py" \
        "$T/translations.xml"
}

@test "a generation that dies after writing the master fails and leaves no master" {
    GEN_DIES=1 run run_rule
    [ "$status" -ne 0 ]
    # The discriminator: the recipe must not leave a fresh-mtime master for
    # the next build to treat as current.
    [ ! -e "$T/translations.xml" ]
}

@test "a successful generation keeps the master and prints the checkmark" {
    run run_rule
    [ "$status" -eq 0 ]
    [ -e "$T/translations.xml" ]
    [ -e "$T/de.xml" ]
    grep -q 'Translations generated' <<<"$output"
}

@test "both generation branches drop the master on failure" {
    # The behavioral test above reaches the venv branch; the system-python3
    # branch carries the same guard or a host without the venv regains the
    # partial-artifact hole.
    [ "$(grep -c -- '|| { rm -f $@; exit 1; };' mk/translations.mk)" -eq 2 ]
}
