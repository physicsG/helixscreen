#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
#
# One-line install entry point:
#
#   curl -sSL https://raw.githubusercontent.com/prestonbrown/helixscreen/main/scripts/install.sh | sh
#
# Install guides, forum posts and older docs link to this path, so it has to
# keep answering. The real installer is bundled per release by `make
# installer` and published at https://releases.helixscreen.org/install.sh,
# which is the URL docs should give. This fetches that bundle and runs it with
# the same arguments.

set -e

url="${HELIX_INSTALLER_URL:-https://releases.helixscreen.org/install.sh}"
tmp="${TMPDIR:-/tmp}/helixscreen-install.$$.sh"
trap 'rm -f "$tmp"' EXIT
trap 'rm -f "$tmp"; exit 130' INT TERM

if command -v curl >/dev/null 2>&1; then
    curl -fsSL "$url" -o "$tmp"
elif command -v wget >/dev/null 2>&1; then
    wget -q -O "$tmp" "$url"
else
    echo "HelixScreen: curl or wget is needed to download the installer from $url" >&2
    exit 1
fi

if [ ! -s "$tmp" ]; then
    echo "HelixScreen: downloaded an empty installer from $url" >&2
    exit 1
fi

sh "$tmp" "$@"
