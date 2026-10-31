#!/usr/bin/env bats
# SPDX-License-Identifier: GPL-3.0-or-later
#
# scripts/install.sh is the bootstrap behind the one-line install URL on main.
# It downloads the release-bundled installer and runs it with the caller's
# arguments; these cases fake the download so nothing leaves the machine.

setup() {
    load helpers
    cd "$BATS_TEST_DIRNAME/../.." || return 1
    BOOTSTRAP="$PWD/scripts/install.sh"
    export TMPDIR="$BATS_TEST_TMPDIR/tmp"
    mkdir -p "$TMPDIR"
    # The installer a download delivers: it reports its arguments.
    FAKE_INSTALLER="$BATS_TEST_TMPDIR/fake-installer.sh"
    printf '#!/bin/sh\necho "installer ran with: $*"\n' > "$FAKE_INSTALLER"
    URL_LOG="$BATS_TEST_TMPDIR/url.log"
}

# curl that records the URL it was asked for and writes the fake installer to -o.
mock_curl_serving() {
    local body="$1"
    mock_command_script "curl" "
out=''; url=''
while [ \$# -gt 0 ]; do
    case \"\$1\" in
        -o) out=\"\$2\"; shift 2 ;;
        -*) shift ;;
        *) url=\"\$1\"; shift ;;
    esac
done
echo \"\$url\" >> \"$URL_LOG\"
cp \"$body\" \"\$out\""
}

@test "downloads the release installer and runs it with the caller's arguments" {
    mock_curl_serving "$FAKE_INSTALLER"
    run sh "$BOOTSTRAP" --update --yes
    [ "$status" -eq 0 ]
    [ "$output" = "installer ran with: --update --yes" ]
    run cat "$URL_LOG"
    [ "$output" = "https://releases.helixscreen.org/install.sh" ]
}

@test "the downloaded installer is removed after it runs" {
    mock_curl_serving "$FAKE_INSTALLER"
    run sh "$BOOTSTRAP"
    [ "$status" -eq 0 ]
    run find "$TMPDIR" -name 'helixscreen-install.*'
    [ -z "$output" ]
}

@test "the installer's exit status is the bootstrap's" {
    printf '#!/bin/sh\nexit 7\n' > "$FAKE_INSTALLER"
    mock_curl_serving "$FAKE_INSTALLER"
    run sh "$BOOTSTRAP"
    [ "$status" -eq 7 ]
}

@test "an empty download is refused instead of run" {
    : > "$BATS_TEST_TMPDIR/empty.sh"
    mock_curl_serving "$BATS_TEST_TMPDIR/empty.sh"
    run sh "$BOOTSTRAP"
    [ "$status" -ne 0 ]
    [[ "$output" == *"empty installer"* ]]
}

@test "a failed download stops before running anything" {
    mock_command_script "curl" "exit 22"
    run sh "$BOOTSTRAP"
    [ "$status" -ne 0 ]
    [[ "$output" != *"installer ran"* ]]
}

@test "falls back to wget when curl is absent" {
    # sandbox-gap-reviewed: the replaced PATH holds only a mock wget and links
    # to sh, cp, rm and cat, so nothing here can reach the host.
    local bin="$BATS_TEST_TMPDIR/wget-only"
    mkdir -p "$bin"
    cat > "$bin/wget" <<EOF
#!/bin/sh
out=''; url=''
while [ \$# -gt 0 ]; do
    case "\$1" in
        -O) out="\$2"; shift 2 ;;
        -*) shift ;;
        *) url="\$1"; shift ;;
    esac
done
echo "\$url" >> "$URL_LOG"
cp "$FAKE_INSTALLER" "\$out"
EOF
    chmod +x "$bin/wget"
    for tool in sh cp rm cat; do ln -s "$(command -v "$tool")" "$bin/$tool"; done
    run env PATH="$bin" sh "$BOOTSTRAP" --local x.zip
    [ "$status" -eq 0 ]
    [ "$output" = "installer ran with: --local x.zip" ]
}

@test "HELIX_INSTALLER_URL names a different installer source" {
    mock_curl_serving "$FAKE_INSTALLER"
    HELIX_INSTALLER_URL="https://example.invalid/i.sh" run sh "$BOOTSTRAP"
    [ "$status" -eq 0 ]
    run cat "$URL_LOG"
    [ "$output" = "https://example.invalid/i.sh" ]
}
