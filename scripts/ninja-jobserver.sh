#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# ninja-jobserver.sh: print the path of a ninja that joins a GNU make
# jobserver (1.13 or later), fetching the pinned upstream release once into a
# cache, or exit 1.
#
# ninja 1.13 is a jobserver client in the FIFO form only (MAKEFLAGS carrying
# --jobserver-auth=fifo:PATH, and no -j on its command line), which is how
# scripts/pool-docker.sh lets idf.py's ninja draw from the machine's jobpool.
# The ESP-IDF image ships 1.11, which ignores a jobserver.
#
# The binary is checked against a pinned sha256 when fetched and must run here,
# so it is only offered for this host's own Linux architecture. After the first
# fetch it works offline. Anything that goes wrong (no network, a mismatch, an
# unknown architecture) prints one line and exits 1, and the caller keeps its
# fixed-share path.
#
#   HELIX_NINJA_CACHE    cache dir (default ~/.cache/helixscreen/ninja-<version>)
#   HELIX_NINJA_URL      where to fetch the release zip from
#   HELIX_NINJA_SHA256   its expected sha256
set -u

VERSION=1.13.2
case "$(uname -s)/$(uname -m)" in
    Linux/x86_64) asset=ninja-linux.zip sha=5749cbc4e668273514150a80e387a957f933c6ed3f5f11e03fb30955e2bbead6 ;;
    Linux/aarch64 | Linux/arm64) asset=ninja-linux-aarch64.zip sha=fd2cacc8050a7f12a16a2e48f9e06fca5c14fc4c2bee2babb67b58be17a607fc ;;
    *) echo "ninja-jobserver: no ninja $VERSION for $(uname -s)/$(uname -m)" >&2; exit 1 ;;
esac
url=${HELIX_NINJA_URL:-https://github.com/ninja-build/ninja/releases/download/v$VERSION/$asset}
sha=${HELIX_NINJA_SHA256:-$sha}
cache=${HELIX_NINJA_CACHE:-${XDG_CACHE_HOME:-$HOME/.cache}/helixscreen/ninja-$VERSION}
bin=$cache/ninja

# A ninja that runs here and reports 1.13 or later.
usable() {
    local v
    [ -x "$1" ] && v=$("$1" --version 2>/dev/null) || return 1
    case $v in 1.1[3-9]* | 1.[2-9][0-9]* | [2-9].* | [1-9][0-9]*.*) return 0 ;; esac
    return 1
}

if usable "$bin"; then
    printf '%s\n' "$bin"
    exit 0
fi

mkdir -p "$cache" 2>/dev/null || { echo "ninja-jobserver: cannot create $cache" >&2; exit 1; }
tmp=$(mktemp -d "$cache/.fetch.XXXXXX") || exit 1
trap 'rm -rf -- "${tmp:?}"' EXIT
if ! curl -fsSL --max-time 120 -o "$tmp/ninja.zip" "$url" 2>/dev/null; then
    echo "ninja-jobserver: cannot fetch $url (offline?)" >&2
    exit 1
fi
got=$(sha256sum "$tmp/ninja.zip" | cut -d' ' -f1)
if [ "$got" != "$sha" ]; then
    echo "ninja-jobserver: $url has sha256 $got, expected $sha" >&2
    exit 1
fi
if ! python3 -I -c 'import sys, zipfile; open(sys.argv[2], "wb").write(zipfile.ZipFile(sys.argv[1]).read("ninja"))' \
    "$tmp/ninja.zip" "$tmp/ninja" 2>/dev/null; then
    echo "ninja-jobserver: $url holds no ninja" >&2
    exit 1
fi
chmod 755 "$tmp/ninja"
if ! usable "$tmp/ninja"; then
    echo "ninja-jobserver: the fetched ninja does not run here or is older than 1.13" >&2
    exit 1
fi
mv -f "$tmp/ninja" "$bin"
printf '%s\n' "$bin"
