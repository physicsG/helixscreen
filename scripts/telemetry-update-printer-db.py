#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""
Interactive Printer Database Updater

Consumes output from telemetry-printer-profiles.py (--json --candidates)
and walks the operator through augmenting/creating printer_database.json
entries and generating preset files.

Usage:
    # Full pipeline:
    ./scripts/telemetry-pull.sh --since 2026-01-01
    ./scripts/telemetry-printer-profiles.py --json --candidates > /tmp/analysis.json
    ./scripts/telemetry-update-printer-db.py /tmp/analysis.json

    # Dry run (show diff without writing):
    ./scripts/telemetry-update-printer-db.py /tmp/analysis.json --dry-run
"""

import argparse
import copy
import difflib
import json
import os
import shutil
import sys
from pathlib import Path
from typing import Any, Optional


# ---------------------------------------------------------------------------
# Data loading
# ---------------------------------------------------------------------------


def load_analysis(path: str) -> dict:
    """Load the JSON output from telemetry-printer-profiles.py."""
    try:
        with open(path) as f:
            data = json.load(f)
    except (json.JSONDecodeError, OSError) as e:
        print(f"ERROR: Cannot load analysis file: {e}", file=sys.stderr)
        sys.exit(1)

    required_keys = ["total_profiles", "model_distribution"]
    for key in required_keys:
        if key not in data:
            print(
                f"ERROR: Analysis JSON missing required key '{key}'. "
                f"Was it generated with --json --candidates?",
                file=sys.stderr,
            )
            sys.exit(1)

    return data


def load_printer_db(path: str) -> dict:
    """Load printer_database.json."""
    try:
        with open(path) as f:
            data = json.load(f)
    except (json.JSONDecodeError, OSError) as e:
        print(f"ERROR: Cannot load printer database: {e}", file=sys.stderr)
        sys.exit(1)

    if "printers" not in data or not isinstance(data["printers"], list):
        print("ERROR: printer_database.json has no 'printers' array", file=sys.stderr)
        sys.exit(1)

    return data


def find_entry_by_name(db: dict, name: str) -> Optional[dict]:
    """Find a printer entry by its 'name' field."""
    for entry in db["printers"]:
        if entry.get("name") == name:
            return entry
    return None


def find_entry_by_id(db: dict, entry_id: str) -> Optional[dict]:
    """Find a printer entry by its 'id' field."""
    for entry in db["printers"]:
        if entry.get("id") == entry_id:
            return entry
    return None


# ---------------------------------------------------------------------------
# Heuristic comparison helpers
# ---------------------------------------------------------------------------


def heuristic_key(h: dict) -> tuple[str, str]:
    """Return (type, pattern) as a comparison key for a heuristic."""
    return (h.get("type", ""), h.get("pattern", ""))


def find_matching_heuristic(
    existing: list[dict], candidate: dict
) -> Optional[dict]:
    """Find an existing heuristic with the same type+pattern."""
    ckey = heuristic_key(candidate)
    for h in existing:
        if heuristic_key(h) == ckey:
            return h
    return None


def get_model_stats(model_dist: dict, model: str) -> tuple[int, int]:
    """Return (profile_count, unique_device_count) for a model."""
    val = model_dist.get(model, 0)
    if isinstance(val, dict):
        return val.get("profiles", 0), val.get("unique_devices", 0)
    return val, val  # old format: same number for both


def deduplicate_candidates(candidates: list[dict]) -> list[dict]:
    """Remove object_exists candidates that duplicate a typed match.

    e.g., macro_match "FOO" + object_exists "gcode_macro FOO" -> keep only macro_match.
    """
    # Build set of patterns covered by typed matches
    typed_patterns: set[str] = set()
    for c in candidates:
        ctype = c.get("type", "")
        pat = c.get("pattern", "")
        if ctype == "macro_match":
            typed_patterns.add(f"gcode_macro {pat}")
            typed_patterns.add(f"gcode_macro {pat.lower()}")
        elif ctype == "fan_match":
            typed_patterns.add(pat)
        elif ctype == "sensor_match":
            typed_patterns.add(pat)
            typed_patterns.add(f"temperature_sensor {pat}")
            typed_patterns.add(f"filament_switch_sensor {pat}")
        elif ctype == "led_match":
            typed_patterns.add(pat)

    result = []
    for c in candidates:
        if c.get("type") == "object_exists" and c.get("pattern", "") in typed_patterns:
            continue  # redundant
        result.append(c)
    return result


def find_substring_heuristic(
    existing: list[dict], candidate: dict
) -> Optional[dict]:
    """Find an existing heuristic where pattern is a substring match."""
    ctype = candidate.get("type", "")
    cpat = candidate.get("pattern", "")
    if not cpat:
        return None
    for h in existing:
        if h.get("type") != ctype:
            continue
        hpat = h.get("pattern", "")
        if not hpat:
            continue
        if cpat in hpat or hpat in cpat:
            if cpat != hpat:  # not exact match (handled elsewhere)
                return h
    return None


# ---------------------------------------------------------------------------
# Interactive prompt helpers
# ---------------------------------------------------------------------------


def prompt(message: str, valid: Optional[set[str]] = None) -> str:
    """Prompt the user for input. Returns stripped lowercase response."""
    try:
        response = input(message).strip()
    except (EOFError, KeyboardInterrupt):
        print("\nAborted — no changes written.")
        sys.exit(0)

    if valid and response.lower() not in valid:
        return response  # caller decides how to handle
    return response


def prompt_yes_no(message: str, default: bool = True) -> bool:
    """Prompt for yes/no with a default."""
    suffix = " [Y/n] " if default else " [y/N] "
    response = prompt(message + suffix)
    if not response:
        return default
    return response.lower().startswith("y")


def format_heuristic_short(h: dict) -> str:
    """Format a heuristic as a one-line summary."""
    return f'{h.get("type", "?")} "{h.get("pattern", "?")}" (confidence: {h.get("confidence", "?")})'


# ---------------------------------------------------------------------------
# Mode 1: Augment existing entries
# ---------------------------------------------------------------------------


def run_augment_existing(
    analysis: dict,
    db: dict,
    min_confidence: int,
    max_candidates: int = 10,
) -> list[str]:
    """
    Walk through candidate heuristics for detected models.
    Modifies db in place. Returns list of modified model names.
    """
    candidates_by_model = analysis.get("candidate_heuristics_detected", {})
    model_dist = analysis.get("model_distribution", {})
    modified_models: list[str] = []

    if not candidates_by_model:
        print("\n  No candidate heuristics for existing models.\n")
        return modified_models

    for model, candidates in sorted(
        candidates_by_model.items(),
        key=lambda x: -get_model_stats(model_dist, x[0])[1],
    ):
        entry = find_entry_by_name(db, model)
        if entry is None:
            continue

        profiles, devices = get_model_stats(model_dist, model)
        existing_heuristics = entry.get("heuristics", [])

        # Filter candidates by min_confidence and novelty
        new_candidates: list[dict] = []
        confidence_updates: list[tuple[dict, dict]] = []  # (existing, candidate)
        substring_warnings: list[tuple[dict, dict]] = []  # (existing, candidate)

        for c in candidates:
            if c.get("confidence", 0) < min_confidence:
                continue

            exact = find_matching_heuristic(existing_heuristics, c)
            if exact:
                if exact.get("confidence") != c.get("confidence"):
                    confidence_updates.append((exact, c))
                continue  # exact duplicate — skip

            substr = find_substring_heuristic(existing_heuristics, c)
            if substr:
                substring_warnings.append((substr, c))
                continue

            new_candidates.append(c)

        if not new_candidates and not confidence_updates:
            continue

        # Deduplicate and cap candidates
        new_candidates = deduplicate_candidates(new_candidates)
        new_candidates.sort(key=lambda h: h.get("confidence", 0), reverse=True)
        new_candidates = new_candidates[:max_candidates]

        if not new_candidates and not confidence_updates:
            continue

        # Display header
        print(f"\n{'━' * 60}")
        print(f"  {model} ({devices} devices, {profiles} profiles)")
        print(f"{'━' * 60}")
        print(f"  Existing heuristics: {len(existing_heuristics)}")

        # Handle confidence mismatches first
        for existing_h, cand_h in confidence_updates:
            print(
                f"\n  [!] {format_heuristic_short(existing_h)}"
                f"\n      Telemetry suggests confidence: {cand_h.get('confidence')}"
                f"\n      {cand_h.get('reason', '')}"
            )
            resp = prompt("      (u)pdate confidence, (s)kip > ")
            if resp.lower() == "u":
                existing_h["confidence"] = cand_h["confidence"]
                existing_h["reason"] = cand_h.get("reason", existing_h.get("reason", ""))
                if model not in modified_models:
                    modified_models.append(model)

        # Auto-skip substring matches (just inform, don't prompt)
        for existing_h, cand_h in substring_warnings:
            print(
                f"  [skipped] \"{cand_h.get('pattern', '?')}\" — "
                f"already covered by \"{existing_h.get('pattern', '?')}\""
            )

        if not new_candidates:
            continue

        # Display new candidates
        print(f"  New candidates: {len(new_candidates)}\n")
        selected = [True] * len(new_candidates)

        for i, c in enumerate(new_candidates):
            mark = "x" if selected[i] else " "
            print(f"  [{mark}] {i + 1}. {format_heuristic_short(c)}")
            print(f"       {c.get('reason', '')}")
            print()

        # Prompt for action
        while True:
            resp = prompt(
                f"  (d)one — add selected, (a)ll — select all, "
                f"(1-{len(new_candidates)}) toggle, (c) N V, (v)iew, (s)kip\n> "
            )
            resp_lower = resp.lower().strip()

            if resp_lower == "s":
                break
            elif resp_lower == "d":
                count = sum(selected)
                if count == 0:
                    print("  Nothing selected. Use (a)ll or toggle numbers first.")
                    continue
                for i, c in enumerate(new_candidates):
                    if selected[i]:
                        existing_heuristics.append(c)
                if model not in modified_models:
                    modified_models.append(model)
                print(f"  Added {count} heuristic(s) to {model}.")
                break
            elif resp_lower == "a":
                selected = [True] * len(new_candidates)
                # Re-display with checkmarks
                for i, c in enumerate(new_candidates):
                    mark = "x" if selected[i] else " "
                    print(f"  [{mark}] {i + 1}. {format_heuristic_short(c)}")
                print("  All selected. Press (d)one to add.")
            elif resp_lower == "v":
                print(json.dumps(entry, indent=2))
            elif resp_lower.startswith("c"):
                # Parse "c N V" — set candidate N's confidence to V
                parts = resp_lower.split()
                if len(parts) == 3:
                    try:
                        idx = int(parts[1]) - 1
                        val = int(parts[2])
                        if 0 <= idx < len(new_candidates) and 0 < val <= 100:
                            new_candidates[idx]["confidence"] = val
                            print(
                                f"  Updated [{idx + 1}] confidence to {val}."
                            )
                        else:
                            print("  Invalid index or value.")
                    except ValueError:
                        print("  Usage: c <number> <confidence>")
                else:
                    print("  Usage: c <number> <confidence>")
            else:
                # Try parsing as a number to toggle
                try:
                    idx = int(resp_lower) - 1
                    if 0 <= idx < len(new_candidates):
                        selected[idx] = not selected[idx]
                        mark = "x" if selected[idx] else " "
                        print(f"  [{mark}] {idx + 1}. {format_heuristic_short(new_candidates[idx])}")
                    else:
                        print(f"  Invalid number. Use 1-{len(new_candidates)}.")
                except ValueError:
                    print("  Unknown action. Try d, a, s, v, c, or a number.")

    return modified_models


# ---------------------------------------------------------------------------
# Mode 2: Create new printer entries
# ---------------------------------------------------------------------------


def generate_entry_id(name: str) -> str:
    """Generate a database ID from a printer name."""
    return (
        name.lower()
        .replace(" ", "_")
        .replace("-", "_")
        .replace(".", "_")
        .replace("(", "")
        .replace(")", "")
    )


MAX_ENTRY_HEURISTICS = 8


def build_entry(
    entry_id: str,
    name: str,
    manufacturer: str = "",
    image: str = "",
    preset: str = "",
    heuristics: Optional[list[dict]] = None,
) -> dict:
    """Build a printer_database.json entry; blank optional fields are omitted."""
    entry: dict[str, Any] = {"id": entry_id, "name": name}
    if manufacturer:
        entry["manufacturer"] = manufacturer
    if image:
        entry["image"] = image
    if preset:
        entry["preset"] = preset
    entry["heuristics"] = list(heuristics) if heuristics else []
    return entry


def cluster_heuristics(cluster: dict, min_confidence: int) -> list[dict]:
    """A cluster's candidate heuristics at or above min_confidence."""
    return [
        h
        for h in cluster.get("candidate_heuristics", [])
        if h.get("confidence", 0) >= min_confidence
    ]


def draft_cluster_entry(
    cluster: dict, db: dict, min_confidence: int = 75, name: str = ""
) -> dict:
    """Non-interactive entry for an undetected cluster, with an id unused in db."""
    bv = cluster.get("build_volume", {})
    label = name or (
        f"Unidentified {cluster.get('kinematics', 'unknown')} "
        f"{bv.get('x', '?')}x{bv.get('y', '?')}x{bv.get('z', '?')}"
    )
    base_id = generate_entry_id(label)
    entry_id, n = base_id, 2
    while find_entry_by_id(db, entry_id):
        entry_id, n = f"{base_id}_{n}", n + 1
    picked = cluster_heuristics(cluster, min_confidence)[:MAX_ENTRY_HEURISTICS]
    return build_entry(entry_id, label, heuristics=picked)


def run_create_new_entries(
    analysis: dict,
    db: dict,
    min_confidence: int,
) -> list[dict]:
    """
    Walk through undetected clusters and offer to create new entries.
    Modifies db in place. Returns list of new entry dicts.
    """
    clusters = analysis.get("candidate_heuristics_clusters", [])
    new_entries: list[dict] = []

    if not clusters:
        print("\n  No undetected printer clusters.\n")
        return new_entries

    for cluster in clusters:
        cluster_id = cluster.get("cluster_id", "?")
        size = cluster.get("size", 0)
        kin = cluster.get("kinematics", "unknown")
        bv = cluster.get("build_volume", {})
        mcu = cluster.get("mcu", "unknown")

        heuristics = cluster_heuristics(cluster, min_confidence)

        print(f"\n{'━' * 60}")
        print(f"  Unidentified Cluster #{cluster_id} ({size} devices)")
        print(f"{'━' * 60}")
        print(f"  Kinematics: {kin}")
        print(
            f"  Build volume: ~{bv.get('x', '?')}x{bv.get('y', '?')}x{bv.get('z', '?')}mm"
        )
        print(f"  MCU: {mcu}")

        if heuristics:
            print(f"\n  Top discriminating objects:")
            for h in heuristics[:MAX_ENTRY_HEURISTICS]:
                print(f"    {format_heuristic_short(h)}")
                print(f"      {h.get('reason', '')}")

        print()
        resp = prompt("  Actions: (n)ew entry, (s)kip, (v)iew full detail\n> ")

        if resp.lower() == "v":
            print(json.dumps(cluster, indent=2))
            resp = prompt("\n  Actions: (n)ew entry, (s)kip\n> ")

        if resp.lower() != "n":
            continue

        # Gather entry metadata
        name = prompt("  Printer name: ")
        if not name:
            print("  Skipping — name required.")
            continue

        manufacturer = prompt("  Manufacturer: ")
        default_id = generate_entry_id(name)
        entry_id = prompt(f"  ID [{default_id}]: ") or default_id
        preset = prompt("  Preset (blank for none): ")
        image = prompt("  Image filename (blank for none): ")

        # Check for ID collision
        if find_entry_by_id(db, entry_id):
            print(f"  WARNING: ID '{entry_id}' already exists in database!")
            if not prompt_yes_no("  Continue anyway?", default=False):
                continue

        # Select heuristics
        selected_heuristics: list[dict] = []
        if heuristics:
            print(f"\n  Include suggested heuristics?")
            for i, h in enumerate(heuristics[:MAX_ENTRY_HEURISTICS]):
                print(f"    [{i + 1}] {format_heuristic_short(h)}")

            h_resp = prompt(
                f"  (a)ll, (1-{min(MAX_ENTRY_HEURISTICS, len(heuristics))}) select by number, (n)one\n> "
            )
            if h_resp.lower() == "a":
                selected_heuristics = list(heuristics[:MAX_ENTRY_HEURISTICS])
            elif h_resp.lower() != "n":
                for part in h_resp.replace(",", " ").split():
                    try:
                        idx = int(part) - 1
                        if 0 <= idx < min(MAX_ENTRY_HEURISTICS, len(heuristics)):
                            selected_heuristics.append(heuristics[idx])
                    except ValueError:
                        pass

        entry = build_entry(
            entry_id, name, manufacturer, image, preset, selected_heuristics
        )

        db["printers"].append(entry)
        new_entries.append(entry)
        print(f"\n  Entry '{name}' created with {len(selected_heuristics)} heuristic(s).")

        # Offer preset generation immediately
        if preset:
            if prompt_yes_no(f"  Generate preset file?"):
                entry["_generate_preset"] = True

    return new_entries


# ---------------------------------------------------------------------------
# Mode 3: Preset generation
# ---------------------------------------------------------------------------


def run_preset_generation(
    db: dict,
    modified_models: list[str],
    new_entries: list[dict],
    presets_dir: Path,
    dry_run: bool,
) -> list[str]:
    """
    Check for missing preset files for modified/new entries.
    Returns list of preset file paths created.
    """
    created_presets: list[str] = []

    entries_to_check: list[dict] = []
    for entry in new_entries:
        if entry.get("_generate_preset"):
            entries_to_check.append(entry)

    # Also check modified entries with preset fields but no file
    for model_name in modified_models:
        entry = find_entry_by_name(db, model_name)
        if entry and entry.get("preset"):
            preset_file = presets_dir / f"{entry['preset']}.json"
            if not preset_file.exists():
                entries_to_check.append(entry)

    for entry in entries_to_check:
        preset_name = entry.get("preset", "")
        if not preset_name:
            continue

        preset_path = presets_dir / f"{preset_name}.json"
        if preset_path.exists():
            continue

        # For entries flagged during creation, don't re-prompt
        if not entry.get("_generate_preset"):
            if not prompt_yes_no(
                f"\n  Preset file missing for '{entry.get('name')}' "
                f"(preset: \"{preset_name}\"). Generate?"
            ):
                continue

        scaffold = {
            "preset": preset_name,
            "wizard_completed": False,
            "display": {},
            "printer": {
                "heaters": {
                    "bed": "heater_bed",
                    "hotend": "extruder",
                },
                "temp_sensors": {
                    "bed": "heater_bed",
                    "hotend": "extruder",
                },
                "fans": {},
                "leds": {},
            },
        }

        if dry_run:
            print(f"\n  [dry-run] Would create: {preset_path}")
            print(json.dumps(scaffold, indent=2))
        else:
            preset_path.parent.mkdir(parents=True, exist_ok=True)
            with open(preset_path, "w") as f:
                json.dump(scaffold, f, indent=2)
                f.write("\n")
            print(f"  Created: {preset_path}")

        created_presets.append(str(preset_path))

    return created_presets


# ---------------------------------------------------------------------------
# Output: write DB + summary
# ---------------------------------------------------------------------------


def write_database(db: dict, db_path: str, dry_run: bool) -> None:
    """Write printer_database.json with backup and atomic rename."""
    # Clean up internal flags
    for entry in db["printers"]:
        entry.pop("_generate_preset", None)

    new_json = json.dumps(db, indent=2, ensure_ascii=False) + "\n"

    if dry_run:
        # Show unified diff
        try:
            with open(db_path) as f:
                old_json = f.read()
        except OSError:
            old_json = ""

        diff = difflib.unified_diff(
            old_json.splitlines(keepends=True),
            new_json.splitlines(keepends=True),
            fromfile=f"{db_path} (original)",
            tofile=f"{db_path} (updated)",
        )
        diff_text = "".join(diff)
        if diff_text:
            print(f"\n{'━' * 60}")
            print("  Diff (dry run):")
            print(f"{'━' * 60}")
            print(diff_text)
        else:
            print("\n  No changes to printer_database.json.")
        return

    # Backup
    bak_path = db_path + ".bak"
    if os.path.exists(db_path):
        shutil.copy2(db_path, bak_path)

    # Atomic write via temp file
    tmp_path = db_path + ".tmp"
    with open(tmp_path, "w") as f:
        f.write(new_json)
    os.replace(tmp_path, db_path)

    print(f"  Backup: {bak_path}")


def print_summary(
    modified_models: list[str],
    new_entries: list[dict],
    created_presets: list[str],
    dry_run: bool,
) -> None:
    """Print a change summary."""
    print(f"\n{'━' * 60}")
    prefix = "  [dry-run] " if dry_run else "  "
    print(f"{prefix}Summary")
    print(f"{'━' * 60}")

    if modified_models:
        names = ", ".join(modified_models)
        print(f"{prefix}Modified entries: {len(modified_models)} ({names})")
    else:
        print(f"{prefix}Modified entries: 0")

    if new_entries:
        names = ", ".join(e.get("name", "?") for e in new_entries)
        print(f"{prefix}New entries: {len(new_entries)} ({names})")
    else:
        print(f"{prefix}New entries: 0")

    if created_presets:
        for p in created_presets:
            print(f"{prefix}New preset: {p}")
    else:
        print(f"{prefix}New presets: 0")

    print()


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------


def main():
    parser = argparse.ArgumentParser(
        description=(
            "Interactive printer database updater. "
            "Consumes telemetry-printer-profiles.py --json --candidates output."
        )
    )
    parser.add_argument(
        "analysis_json",
        help="Path to analysis JSON from telemetry-printer-profiles.py --json --candidates",
    )
    parser.add_argument(
        "--db",
        default="assets/config/printer_database.json",
        help="Path to printer_database.json (default: assets/config/printer_database.json)",
    )
    parser.add_argument(
        "--presets-dir",
        default="assets/config/presets/",
        help="Path to presets directory (default: assets/config/presets/)",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Show what would change without writing files",
    )
    parser.add_argument(
        "--skip-existing",
        action="store_true",
        help="Skip augmentation of existing entries",
    )
    parser.add_argument(
        "--skip-new",
        action="store_true",
        help="Skip creation of new entries",
    )
    parser.add_argument(
        "--min-confidence",
        type=int,
        default=75,
        help="Only suggest heuristics with confidence >= N (default: 75)",
    )
    parser.add_argument(
        "--max-candidates",
        type=int,
        default=10,
        help="Max candidate heuristics per model (default: 10)",
    )
    args = parser.parse_args()

    # Load data
    analysis = load_analysis(args.analysis_json)
    db = load_printer_db(args.db)
    original_db = copy.deepcopy(db)

    total = analysis.get("total_profiles", 0)
    detected = analysis.get("detected_count", 0)
    undetected = analysis.get("undetected_count", 0)

    print(f"\n{'━' * 60}")
    print(f"  Printer Database Updater")
    print(f"{'━' * 60}")
    print(f"  Telemetry profiles: {total} ({detected} detected, {undetected} undetected)")
    print(f"  Database entries:   {len(db['printers'])}")
    print(f"  Min confidence:     {args.min_confidence}")
    if args.dry_run:
        print(f"  Mode:               DRY RUN")
    print()

    # Mode 1: Augment existing entries
    modified_models: list[str] = []
    if not args.skip_existing:
        modified_models = run_augment_existing(
            analysis, db, args.min_confidence, args.max_candidates
        )

    # Mode 2: Create new printer entries
    new_entries: list[dict] = []
    if not args.skip_new:
        new_entries = run_create_new_entries(analysis, db, args.min_confidence)

    # Mode 3: Preset generation
    presets_dir = Path(args.presets_dir)
    created_presets = run_preset_generation(
        db, modified_models, new_entries, presets_dir, args.dry_run
    )

    # Write results
    if modified_models or new_entries:
        write_database(db, args.db, args.dry_run)

    # Summary
    print_summary(modified_models, new_entries, created_presets, args.dry_run)


if __name__ == "__main__":
    main()
