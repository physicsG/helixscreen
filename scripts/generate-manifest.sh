#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# generate-manifest.sh — Generate manifest.json from a directory of release tarballs.
# Shared by CI (release.yml) and local dev releases (dev-release.sh).

set -euo pipefail

# Default values
VERSION=""
TAG=""
NOTES=""
DIR=""
BASE_URL=""
OUTPUT=""
INCLUDE_ZIP=true

# Platforms whose ALREADY-DEPLOYED clients cannot verify a zip, so the manifest
# must keep pointing them at the tar.gz (prestonbrown/helixscreen#993).
#
# A pre-v0.99.102 in-app updater verifies a download with `unzip -tqq`. BusyBox
# only grew `unzip -t` in 1.32 — the K1 ships 1.31.1 and the AD5M 1.29.3 — and
# the K2's OpenWrt has no unzip binary or applet at all. Those clients reject a
# byte-perfect zip as "Corrupt download": the tool rejects the invocation, not
# the archive.
#
# v0.99.102 fixes the verifier, but that fix ships INSIDE the package the broken
# verifier refuses to install, so a client-side fix cannot bootstrap itself. The
# manifest is the only lever that reaches an already-deployed binary. Withholding
# zip_url is what pins those clients to the tar.gz, because
# `src/system/update_checker.cpp#populate_release_urls_from_manifest` prefers
# zip_url whenever the manifest carries one. The tar.gz path verifies with
# `gunzip -t`, which works on every BusyBox in the fleet.
#
# REMOVING A PLATFORM FROM THIS LIST IS IRREVERSIBLE for any client that cannot
# verify a zip: hand it a zip_url once and it can never self-update again, so no
# later manifest reaches it. Confirm with `zip_readiness_by_platform` from
# `scripts/telemetry-analyze.py` over at least a 90-day window before removing
# one. A 30-day window reports every platform as converged and cannot see the
# dormant devices that are exactly the risk. Use --zip-exclude to try a shorter
# list without editing this file.
ZIP_EXCLUDE_PLATFORMS="ad5m ad5x cc1 k1 k2 snapmaker-u1"

usage() {
    cat <<EOF
Usage: generate-manifest.sh --version VERSION --tag TAG --notes NOTES --dir DIR --base-url URL --output FILE [--include-zip]

Generate a manifest.json from release tarballs in DIR.

Options:
  --version VERSION   Version string (e.g., "0.9.5")
  --tag TAG           Git tag (e.g., "v0.9.5")
  --notes NOTES       Release notes text
  --dir DIR           Directory containing helixscreen-{platform}-*.tar.gz
                      and/or helixscreen-{platform}.zip files
  --base-url URL      Base URL for download links (e.g., "https://releases.helixscreen.org/dev")
  --output FILE       Output manifest.json path
  --include-zip       Include zip_url/zip_sha256 fields when a .zip is present.
                      ON by default (adoption telemetry 2026-07 shows the
                      pre-v0.99.31 population is ~0.6% and no longer updating).
                      The legacy url/sha256 fields still point at the .tar.gz, so
                      pre-v0.99.31 in-app updaters — which never read zip_url —
                      keep working; v0.99.31+ clients prefer zip_url. Accepted as
                      a no-op for backward compatibility.
  --no-include-zip    Suppress the zip_url/zip_sha256 fields (legacy behavior;
                      only needed to protect a resurgent pre-v0.99.31 fleet).
  --zip-exclude LIST  Space-separated platforms to withhold zip_url from, even
                      when a .zip is present. REPLACES the built-in list
                      ("$ZIP_EXCLUDE_PLATFORMS"),
                      so pass "" to offer zip everywhere. These platforms still
                      get a complete tar.gz asset. The default covers the
                      BusyBox/OpenWrt devices whose deployed pre-v0.99.102
                      updaters reject an intact zip (helixscreen#993); dropping
                      a platform is one-way, so confirm zip readiness over a
                      90-day window first.
  --help              Show this help message
EOF
    exit 0
}

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --version)     VERSION="$2";     shift 2 ;;
        --tag)         TAG="$2";         shift 2 ;;
        --notes)       NOTES="$2";       shift 2 ;;
        --dir)         DIR="$2";         shift 2 ;;
        --base-url)    BASE_URL="$2";    shift 2 ;;
        --output)      OUTPUT="$2";      shift 2 ;;
        --include-zip)    INCLUDE_ZIP=true;  shift ;;
        --no-include-zip) INCLUDE_ZIP=false; shift ;;
        --zip-exclude) ZIP_EXCLUDE_PLATFORMS="$2"; shift 2 ;;
        --help)        usage ;;
        *)
            echo "Error: Unknown option $1" >&2
            exit 1
            ;;
    esac
done

# Validate required arguments
missing=()
[[ -z "$VERSION" ]] && missing+=("--version")
[[ -z "$TAG" ]]     && missing+=("--tag")
[[ -z "$DIR" ]]     && missing+=("--dir")
[[ -z "$BASE_URL" ]] && missing+=("--base-url")
[[ -z "$OUTPUT" ]]  && missing+=("--output")

if [[ ${#missing[@]} -gt 0 ]]; then
    echo "Error: Missing required arguments: ${missing[*]}" >&2
    exit 1
fi

if [[ ! -d "$DIR" ]]; then
    echo "Error: Directory not found: $DIR" >&2
    exit 1
fi

# Check required tools
if ! command -v jq &>/dev/null; then
    echo "Error: jq not found. Please install it." >&2
    exit 1
fi

# Determine sha256 command
if command -v shasum &>/dev/null; then
    SHA256_CMD="shasum -a 256"
elif command -v sha256sum &>/dev/null; then
    SHA256_CMD="sha256sum"
else
    echo "Error: Neither shasum nor sha256sum found" >&2
    exit 1
fi

# Auto-discover platforms from the assets in DIR. A platform qualifies on
# either asset: `helixscreen-{platform}-{version}.tar.gz` or
# `helixscreen-{platform}.zip`. Auto-discovery keeps this script in sync with
# whatever .github/workflows/release.yml uploads — adding a new platform to the
# release matrix doesn't require editing this file.
FOUND_ANY=false
ASSETS_JSON="{}"
PLATFORMS=()
ZIP_GATED=()

# Both discovery loops feed this, and a platform shipping both assets is found
# twice; the emit loop must visit each key once.
add_platform() {
    local candidate="$1" known
    for known in ${PLATFORMS[@]+"${PLATFORMS[@]}"}; do
        [[ "$known" == "$candidate" ]] && return 0
    done
    PLATFORMS+=("$candidate")
}

for f in "$DIR"/helixscreen-*-*.tar.gz; do
    [[ -f "$f" ]] || continue
    base=$(basename "$f")
    # Strip leading 'helixscreen-' and trailing '-{version}.tar.gz' to recover
    # the platform key. Both halves carry hyphens (platforms like snapmaker-u1,
    # prerelease versions like 1.1.0-beta.1), so the split anchors on the last
    # '-v' that opens a version. The 'v' is required, not optional: a purely
    # numeric prerelease identifier (1.1.0-2) is otherwise indistinguishable
    # from a version, and the platform key swallows the real one. Every
    # producer emits it -- cross.mk builds RELEASE_VERSION as v$(VERSION).
    if [[ "$base" =~ ^helixscreen-(.+)-v([0-9][0-9A-Za-z.+-]*)\.tar\.gz$ ]]; then
        add_platform "${BASH_REMATCH[1]}"
    fi
done

# The zip carries no version — cross.mk's release-* recipes write
# `helixscreen-$(platform).zip` — so everything between the prefix and `.zip`
# is the platform key, hyphens included. With no version half to separate,
# there is nothing for a key like snapmaker-u1 or k1-dynamic to be split on.
for f in "$DIR"/helixscreen-*.zip; do
    [[ -f "$f" ]] || continue
    base=$(basename "$f")
    # ESP32 firmware zips sit in the same release directory but are flashed
    # over USB; no updater can install one, so none may appear as a platform.
    [[ "$base" == helixscreen-esp32-* ]] && continue
    if [[ "$base" =~ ^helixscreen-(.+)\.zip$ ]]; then
        add_platform "${BASH_REMATCH[1]}"
    fi
done

for plat in ${PLATFORMS[@]+"${PLATFORMS[@]}"}; do
    tarball=""
    for f in "$DIR"/helixscreen-"${plat}"-*.tar.gz; do
        [[ -f "$f" ]] || continue
        # The glob prefix also matches LONGER platform keys that extend this
        # one (k1 vs k1-dynamic), and the longer name sorts first. Re-parse
        # the platform half and keep only an exact match.
        base=$(basename "$f")
        [[ "$base" =~ ^helixscreen-(.+)-v([0-9][0-9A-Za-z.+-]*)\.tar\.gz$ ]] || continue
        [[ "${BASH_REMATCH[1]}" == "$plat" ]] || continue
        tarball="$f"
        break
    done

    # A platform with no tarball is zip-only: it skips the legacy url/sha256/
    # size fields and still gets its zip asset below.
    emitted=false

    if [[ -n "$tarball" ]]; then
        emitted=true
        filename=$(basename "$tarball")
        sha256=$($SHA256_CMD "$tarball" | awk '{print $1}')
        # `wc -c` is portable across Linux/macOS/BSD (stat(1) flags differ:
        # `-c '%s'` GNU vs `-f '%z'` BSD). The in-app updater reads `size` to
        # compute the staging-directory free-space requirement (1.2× + small
        # buffer); omitting it forces a conservative fixed-size fallback.
        size=$(wc -c < "$tarball" | tr -d ' ')
        url="${BASE_URL}/${filename}"

        ASSETS_JSON=$(echo "$ASSETS_JSON" | jq \
            --arg plat "$plat" \
            --arg url "$url" \
            --arg sha256 "$sha256" \
            --argjson size "$size" \
            '.[$plat] = {url: $url, sha256: $sha256, size: $size}')
    fi

    # Add the corresponding ZIP as the preferred asset (used by Moonraker
    # type:zip updates and v0.99.31+ in-app updaters). The tar.gz url/sha256
    # above stay as the legacy fallback for pre-v0.99.31 clients. On by default;
    # --no-include-zip restores the old suppression.
    # ...unless this platform's deployed clients can't verify a zip, in which
    # case the tar.gz above is all they get. See ZIP_EXCLUDE_PLATFORMS.
    if [[ "$INCLUDE_ZIP" == true ]]; then
        zipfile="$DIR/helixscreen-${plat}.zip"
        if [[ -f "$zipfile" && " $ZIP_EXCLUDE_PLATFORMS " == *" $plat "* ]]; then
            ZIP_GATED+=("$plat")
        elif [[ -f "$zipfile" ]]; then
            zip_sha256=$($SHA256_CMD "$zipfile" | awk '{print $1}')
            zip_size=$(wc -c < "$zipfile" | tr -d ' ')
            zip_url="${BASE_URL}/helixscreen-${plat}.zip"

            # `+=` onto an absent key: jq reads null + {..} as the object, so a
            # zip-only platform lands with just its zip fields.
            ASSETS_JSON=$(echo "$ASSETS_JSON" | jq \
                --arg plat "$plat" \
                --arg zip_url "$zip_url" \
                --arg zip_sha256 "$zip_sha256" \
                --argjson zip_size "$zip_size" \
                '.[$plat] += {zip_url: $zip_url, zip_sha256: $zip_sha256, zip_size: $zip_size}')
            emitted=true
        fi
    fi

    if [[ "$emitted" == true ]]; then
        FOUND_ANY=true
    elif [[ " ${ZIP_GATED[*]-} " == *" $plat "* ]]; then
        # Gating the zip is only safe while a tar.gz remains to serve instead.
        # With neither, the platform drops out of the manifest and its clients
        # stop being offered any update at all.
        echo "Error: $plat is zip-gated and has no tar.gz — nothing to serve" >&2
        exit 1
    else
        # Same outcome, other cause: a zip-only platform with zip fields
        # suppressed. Dropping it quietly ships a manifest whose missing
        # platform looks like one that was never built.
        echo "Error: $plat ships only a zip and --no-include-zip suppressed it — nothing to serve" >&2
        exit 1
    fi
done

if [[ "$FOUND_ANY" == "false" ]]; then
    echo "Error: No helixscreen-*.tar.gz or helixscreen-*.zip assets found in $DIR" >&2
    exit 1
fi

# Generate timestamp
PUBLISHED_AT=$(date -u +"%Y-%m-%dT%H:%M:%SZ")

# Build final manifest
jq -n \
    --arg version "$VERSION" \
    --arg tag "$TAG" \
    --arg notes "${NOTES:-}" \
    --arg published_at "$PUBLISHED_AT" \
    --argjson assets "$ASSETS_JSON" \
    '{
        version: $version,
        tag: $tag,
        notes: $notes,
        published_at: $published_at,
        assets: $assets
    }' > "$OUTPUT"

echo "Generated $OUTPUT with platforms: $(echo "$ASSETS_JSON" | jq -r 'keys | join(", ")')"

# Never let a withheld asset pass silently — a gate that hides what it dropped
# reads as "everything shipped".
if [[ ${#ZIP_GATED[@]} -gt 0 ]]; then
    echo "  zip gated off (deployed clients can't verify zip, helixscreen#993): ${ZIP_GATED[*]}"
    echo "  -> these platforms self-update via the tar.gz url instead"
fi
