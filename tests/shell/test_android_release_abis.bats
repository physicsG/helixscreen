#!/usr/bin/env bats
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The Android release compiles each ABI in its own job (release.yml
# build-android-native), packages them once from the prebuilt libraries
# (build-android), and reads per-ABI ccaches that ccache-warm.yml's warm-android
# writes and cache-prune.yml trims. Four files name the ABI list; when they
# disagree, an ABI either ships missing, builds cold every release, or has its
# warm cache pruned away by another ABI's newer one. Nothing goes red on its own
# in any of those cases.

load helpers

setup() {
    cd "$BATS_TEST_DIRNAME/../.." || return 1
}

# Prints one line per disagreement between the four ABI lists.
abi_list_drift() {
    python3 - <<'PY'
import json, re, yaml

def load(p):
    with open(p) as fh:
        return yaml.safe_load(fh)

gradle = open("android/app/build.gradle").read()
m = re.search(r"def helixAbis = .*?\n\s*:\s*\[([^\]]*)\]", gradle, re.S)
default_abis = [a.strip().strip("'\"") for a in m.group(1).split(",")]

release = load(".github/workflows/release.yml")["jobs"]["build-android-native"]["strategy"]["matrix"]["abi"]
warm = load(".github/workflows/ccache-warm.yml")["jobs"]["warm-android"]["strategy"]["matrix"]["abi"]
prune_run = load(".github/workflows/cache-prune.yml")["jobs"]["prune"]["steps"][0]["run"]
families = re.findall(r'"ccache-release-android-([^"]+)-"', prune_run)

bad = []
for name, abis in [("release.yml build-android-native", release),
                   ("ccache-warm.yml warm-android", warm),
                   ("cache-prune.yml release families", families)]:
    if sorted(abis) != sorted(default_abis):
        bad.append(f"{name}: {sorted(abis)} != build.gradle helixAbis {sorted(default_abis)}")
print("\n".join(bad))
PY
}

@test "every Android ABI list matches build.gradle's default helixAbis" {
    run abi_list_drift
    [ "$status" -eq 0 ] || fail "$output"
    [ -z "$output" ] || fail "$output"
}

@test "the release and the warm compile through the same composite action" {
    run python3 - <<'PY'
import yaml
def uses(path, job):
    with open(path) as fh:
        steps = yaml.safe_load(fh)["jobs"][job]["steps"]
    return [s for s in steps if s.get("uses") == "./.github/actions/android-native-release"]
rel = uses(".github/workflows/release.yml", "build-android-native")
warm = uses(".github/workflows/ccache-warm.yml", "warm-android")
assert len(rel) == 1 and len(warm) == 1, (rel, warm)
assert str(rel[0]["with"].get("save-cache", "false")) == "false", rel
assert str(warm[0]["with"]["save-cache"]) == "true", warm
PY
    [ "$status" -eq 0 ] || fail "$output"
}

@test "the packaging job builds both artifacts from the prebuilt libraries" {
    run python3 - <<'PY'
import yaml
with open(".github/workflows/release.yml") as fh:
    job = yaml.safe_load(fh)["jobs"]["build-android"]
assert "build-android-native" in job["needs"], job["needs"]
steps = {s.get("name"): s for s in job["steps"]}
for name in ("Build Android APKs (release)", "Build Android AAB (release)"):
    run = steps[name]["run"]
    assert "-PhelixPrebuiltJniLibs=" in run, f"{name}: {run}"
    assert "useCcache" not in run, f"{name} still configures a native compile: {run}"
PY
    [ "$status" -eq 0 ] || fail "$output"
}

# Runs cache-prune.yml's own selection program over a cache listing and prints
# the keys it would delete.
doomed_keys() {
    local caches="$1" include_release="$2"
    python3 - "$caches" "$include_release" <<'PY'
import json, re, subprocess, sys, yaml
with open(".github/workflows/cache-prune.yml") as fh:
    step = yaml.safe_load(fh)["jobs"]["prune"]["steps"][0]
prefixes = json.loads(step["env"]["PREFIXES"])
if sys.argv[2] == "yes":
    extra = re.search(r"PREFIXES=\$\(jq -c '\. \+ (\[.*?\])'", step["run"], re.S).group(1)
    prefixes += json.loads(extra)
prog = re.search(r"jq -r --argjson prefixes \"\$PREFIXES\" --argjson keep \"\$KEEP\" '(.*?)' /tmp/caches.json",
                 step["run"], re.S).group(1)
out = subprocess.run(["jq", "-r", "--argjson", "prefixes", json.dumps(prefixes),
                      "--argjson", "keep", str(step["env"]["KEEP"]), prog, sys.argv[1]],
                     check=True, capture_output=True, text=True).stdout
for line in out.splitlines():
    print(line.split("\t")[3])
PY
}

@test "a release prune keeps each Android ABI's newest cache" {
    local caches="$BATS_TEST_TMPDIR/caches.json"
    cat > "$caches" <<'JSON'
[
 {"id":1,"size_in_bytes":1,"ref":"refs/heads/main","created_at":"2026-10-01T06:00:00Z","key":"ccache-release-android-arm64-v8a-old"},
 {"id":2,"size_in_bytes":1,"ref":"refs/heads/main","created_at":"2026-10-02T06:00:00Z","key":"ccache-release-android-arm64-v8a-new"},
 {"id":3,"size_in_bytes":1,"ref":"refs/heads/main","created_at":"2026-10-01T06:00:00Z","key":"ccache-release-android-armeabi-v7a-only"},
 {"id":4,"size_in_bytes":1,"ref":"refs/heads/main","created_at":"2026-10-01T06:00:00Z","key":"ccache-release-android-x86_64-only"},
 {"id":5,"size_in_bytes":1,"ref":"refs/heads/main","created_at":"2026-10-03T06:00:00Z","key":"ccache-release-x86-only"}
]
JSON
    run doomed_keys "$caches" yes
    [ "$status" -eq 0 ] || fail "$output"
    [ "$output" = "ccache-release-android-arm64-v8a-old" ] || fail "expected only the superseded arm64 cache, got: $output"

    run doomed_keys "$caches" no
    [ "$status" -eq 0 ] || fail "$output"
    [ -z "$output" ] || fail "a push-CI prune selected release caches: $output"
}

@test "push CI prunes after its saves and never includes the release caches" {
    run python3 - <<'PY'
import yaml
with open(".github/workflows/build.yml") as fh:
    job = yaml.safe_load(fh)["jobs"]["prune-ccache"]
assert job["uses"] == "./.github/workflows/cache-prune.yml", job
assert not (job.get("with") or {}).get("include_release", False), job
for saver in ("compile-ubuntu", "build-macos", "build-android", "link-aux-binaries"):
    assert saver in job["needs"], f"prune runs without waiting for {saver}"
PY
    [ "$status" -eq 0 ] || fail "$output"
}
