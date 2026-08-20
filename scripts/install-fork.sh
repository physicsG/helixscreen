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
#   # from the fork's latest release (attached to every u1-v* release):
#   curl -fsSL https://github.com/physicsG/helixscreen/releases/latest/download/install-fork.sh | sh
#   # from a branch that carries fork support:
#   HELIX_FORK_REF=<branch> sh -c "$(curl -fsSL https://raw.githubusercontent.com/physicsG/helixscreen/<branch>/scripts/install-fork.sh)"
#   sh install-fork.sh --local helixscreen-snapmaker-u1.zip
#   sh install-fork.sh --version u1-v0.99.114
#   GITHUB_REPO=someone/helixscreen sh install-fork.sh
#
# Every flag is passed straight through to the installer; run with --help to
# see them.

set -e

# Which fork to install from. Override in the environment to use another.
: "${GITHUB_REPO:=physicsG/helixscreen}"

# Git ref to fetch the installer from when neither a local copy nor a release
# provides one. A ref that predates fork support is refused (see below).
: "${HELIX_FORK_REF:=main}"

# The reason this script exists — see the header.
HELIX_GITHUB_ONLY=1

export GITHUB_REPO HELIX_GITHUB_ONLY

RAW_BASE="https://raw.githubusercontent.com/${GITHUB_REPO}/${HELIX_FORK_REF}"
RELEASE_BASE="https://github.com/${GITHUB_REPO}/releases/latest/download"

say()  { printf '%s\n' "$*"; }
die()  { printf 'error: %s\n' "$*" >&2; exit 1; }

# The installer must be one that KNOWS about fork installs. An older bundle
# hard-assigns GITHUB_REPO and has never heard of HELIX_GITHUB_ONLY, so it
# would take the environment this script set, ignore it, and install
# upstream's binary from upstream's CDN — silently, and under the fork's name.
# That is the one outcome this script exists to prevent, so it is checked
# rather than assumed. (Also catches a raw.githubusercontent 404 body that
# some proxies deliver with a 200.)
installer_supports_fork() {
    [ -s "$1" ] && grep -q 'HELIX_GITHUB_ONLY' "$1" 2>/dev/null
}

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
    installer_supports_fork "$installer" || die "the installer beside this script (${installer}) predates fork support"
    say "HelixScreen fork installer"
    say "  repo:      ${GITHUB_REPO} (GitHub releases only)"
    say "  installer: ${installer}"
    say ""
    # shellcheck disable=SC2086
    exec sh "$installer" "$@"
fi

# Otherwise fetch one. Downloaded to a temp file rather than piped into sh: the
# installer reads stdin for confirmations, and a pipe would hand it the rest of
# its own source.
tmp="${TMPDIR:-/tmp}/helixscreen-install.$$.sh"
trap 'rm -f "$tmp"' EXIT INT TERM

fetch() { # url dest -> 0 on success
    if command -v curl >/dev/null 2>&1; then
        curl -fsSL "$1" -o "$2"
    elif command -v wget >/dev/null 2>&1; then
        wget -qO "$2" "$1"
    else
        die "neither curl nor wget is available"
    fi
}

say "HelixScreen fork installer"
say "  repo:      ${GITHUB_REPO} (GitHub releases only)"

# 1. The fork's latest release ships the installer that built its binary — the
#    two cannot drift, and it exists exactly when there is something to
#    install. (Same arrangement as upstream, which attaches install.sh to
#    every release.)
# 2. Failing that, the fork's git ref — but only if that ref's installer knows
#    about fork installs; see installer_supports_fork().
url="${RELEASE_BASE}/install.sh"
say "  fetching:  ${url}"
if fetch "$url" "$tmp" 2>/dev/null && installer_supports_fork "$tmp"; then
    :
else
    rm -f "$tmp"
    url="${RAW_BASE}/scripts/install.sh"
    say "  no release installer; fetching: ${url}"
    fetch "$url" "$tmp" || die "could not download the installer from ${url}"
    installer_supports_fork "$tmp" || die "the installer at ${GITHUB_REPO}@${HELIX_FORK_REF} predates fork support — \
set HELIX_FORK_REF to a branch or tag that has scripts/install-fork.sh"
fi
say ""

exec sh "$tmp" "$@"
