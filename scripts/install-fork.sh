#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Install HelixScreen from a FORK's GitHub releases.
#
# This is a thin front-end for the real installer (scripts/install.sh), not a
# second installer: it sets the two things a fork install needs and hands over.
# Everything else — platform detection, service install, backup/rollback,
# SHA256 verification, --update/--uninstall/--local — is the installer's, so a
# fork install and an upstream install go down the same code path.
#
#   GITHUB_REPO=<owner>/<name>   which repo to install from
#   HELIX_GITHUB_ONLY=1          use that repo's GitHub releases ONLY
#
# The second matters more than it looks. The installer normally tries the
# upstream CDN (releases.helixscreen.org) and HTTP mirror *before* GitHub, and
# those serve upstream's builds — so pointing GITHUB_REPO at a fork without it
# resolves upstream's version number and downloads upstream's binary under it.
#
# Usage:
#   curl -fsSL https://raw.githubusercontent.com/physicsG/helixscreen/main/scripts/install-fork.sh | sh
#   sh install-fork.sh --local helixscreen-snapmaker-u1.zip
#   sh install-fork.sh --version u1-v0.99.114
#   GITHUB_REPO=someone/helixscreen sh install-fork.sh
#
# Every flag is passed straight through to the installer; run with --help to
# see them.

set -e

# Which fork to install from. Override in the environment to use another.
: "${GITHUB_REPO:=physicsG/helixscreen}"

# Git ref to fetch the installer from when it is not already on disk.
: "${HELIX_FORK_REF:=main}"

# The reason this script exists — see the header.
HELIX_GITHUB_ONLY=1

export GITHUB_REPO HELIX_GITHUB_ONLY

RAW_BASE="https://raw.githubusercontent.com/${GITHUB_REPO}/${HELIX_FORK_REF}"

say()  { printf '%s\n' "$*"; }
die()  { printf 'error: %s\n' "$*" >&2; exit 1; }

# A release tarball unpacks with install.sh beside this script, and a git
# checkout has scripts/install.sh — prefer either over the network.
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
installer=""
for candidate in "${script_dir}/install.sh" "${script_dir}/../install.sh"; do
    if [ -f "$candidate" ]; then
        installer="$candidate"
        break
    fi
done

if [ -n "$installer" ]; then
    say "HelixScreen fork installer"
    say "  repo:      ${GITHUB_REPO} (GitHub releases only)"
    say "  installer: ${installer}"
    say ""
    # shellcheck disable=SC2086
    exec sh "$installer" "$@"
fi

# Otherwise fetch it from the fork at the pinned ref. Downloaded to a temp file
# rather than piped into sh: the installer reads stdin for confirmations, and a
# pipe would hand it the rest of its own source.
tmp="${TMPDIR:-/tmp}/helixscreen-install.$$.sh"
trap 'rm -f "$tmp"' EXIT INT TERM

url="${RAW_BASE}/scripts/install.sh"
say "HelixScreen fork installer"
say "  repo:      ${GITHUB_REPO} (GitHub releases only)"
say "  fetching:  ${url}"
say ""

if command -v curl >/dev/null 2>&1; then
    curl -fsSL "$url" -o "$tmp" || die "could not download the installer from ${url}"
elif command -v wget >/dev/null 2>&1; then
    wget -qO "$tmp" "$url" || die "could not download the installer from ${url}"
else
    die "neither curl nor wget is available"
fi

# A 404 from a raw.githubusercontent path arrives as a small HTML/text body with
# a 200 on some proxies; the installer is ~9k lines, so a tiny file is wrong.
if [ ! -s "$tmp" ] || [ "$(wc -c < "$tmp")" -lt 10000 ]; then
    die "downloaded installer looks wrong (too small) — check GITHUB_REPO=${GITHUB_REPO} and HELIX_FORK_REF=${HELIX_FORK_REF}"
fi

exec sh "$tmp" "$@"
