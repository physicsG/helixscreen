#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# check-esp32-manifest.sh MANIFEST DIR: every ESP32 zip in DIR is listed in MANIFEST under
# its esp32-<board> key with the zip's own sha256 and a URL ending in its file name, and
# MANIFEST lists no esp32-* key without a zip in DIR. Local only: run before anything is
# uploaded, so a bad entry stops the release before a manifest goes live.

set -euo pipefail

[[ $# -eq 2 && -f "$1" && -d "$2" ]] || { echo "Usage: $0 MANIFEST DIR" >&2; exit 2; }
manifest="$1"
dir="$2"
bad=0

if command -v sha256sum &>/dev/null; then sha_cmd="sha256sum"; else sha_cmd="shasum -a 256"; fi

declare -A have=()
for f in "$dir"/helixscreen-esp32-*-v*.zip; do
    [[ -f "$f" ]] || continue
    base=$(basename "$f")
    [[ "$base" =~ ^helixscreen-(esp32-.+)-v([0-9][0-9A-Za-z.+-]*)\.zip$ ]] || continue
    key="${BASH_REMATCH[1]}"
    have["$key"]=1
    want=$($sha_cmd "$f" | awk '{print $1}')
    got=$(jq -r --arg k "$key" '.assets[$k].sha256 // empty' "$manifest")
    url=$(jq -r --arg k "$key" '.assets[$k].url // empty' "$manifest")
    if [[ -z "$url" ]]; then
        echo "::error::manifest has no $key entry for $base" >&2; bad=1
    elif [[ "$got" != "$want" ]]; then
        echo "::error::manifest $key sha256 '$got' does not match $base ($want)" >&2; bad=1
    elif [[ "${url##*/}" != "$base" ]]; then
        echo "::error::manifest $key url '$url' does not end in $base" >&2; bad=1
    fi
done

while IFS= read -r key; do
    [[ -n "$key" && -z "${have[$key]:-}" ]] || continue
    echo "::error::manifest lists $key but the release built no matching zip" >&2; bad=1
done < <(jq -r '.assets | keys[] | select(startswith("esp32-"))' "$manifest")

exit "$bad"
