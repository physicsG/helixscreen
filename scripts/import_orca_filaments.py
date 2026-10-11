#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Derive HelixScreen's filaments.json from the OrcaSlicer filament library.

Facts only (temps/densities); no OrcaSlicer profile files are copied or shipped.
"""
import argparse
import json
import os
import re
import sys

# Orca filament_type (open enum) -> our filament_database.h type name.
# Only entries that differ or need pinning are listed; unknown types fall back
# to the raw Orca string (still usable via the product's own temps).
TYPE_MAP = {
    "PLA": "PLA", "PLA-CF": "PLA-CF", "PETG": "PETG", "PETG-CF": "PETG-CF",
    "PET-CF": "PET-CF", "PET-GF": "PET-GF", "ABS": "ABS", "ABS-CF": "ABS-CF",
    "ASA": "ASA", "PC": "PC", "PA": "PA", "PA-CF": "PA-CF", "PA6": "PA6",
    "TPU": "TPU", "PVA": "PVA", "HIPS": "HIPS", "PPS-CF": "PPS-CF",
}

BED_PRIORITY = ("textured_plate_temp", "hot_plate_temp", "cool_plate_temp")


def load_profiles(root: str) -> dict:
    """Map every profile's `name` -> profile dict, across the whole tree."""
    by_name = {}
    for dirpath, _dirs, files in os.walk(root):
        for fn in files:
            if not fn.endswith(".json"):
                continue
            with open(os.path.join(dirpath, fn), encoding="utf-8") as f:
                p = json.load(f)
            p["_src"] = dirpath  # remember source dir for library-scope filtering
            if p.get("type") == "filament" and "name" in p:
                by_name[p["name"]] = p
    return by_name


def resolve_inherits(profile: dict, by_name: dict) -> dict:
    """Flatten the inherits chain: parent first, child overrides."""
    parent_name = profile.get("inherits")
    if parent_name and parent_name in by_name:
        merged = resolve_inherits(by_name[parent_name], by_name)
    else:
        merged = {}
    merged = dict(merged)
    for k, v in profile.items():
        if k in ("inherits", "name", "instantiation"):
            continue
        merged[k] = v
    return merged


def first_scalar(value):
    """Orca stores per-extruder string arrays; return [0] or None for nil/empty."""
    if isinstance(value, list):
        if not value:
            return None
        value = value[0]
    if value is None:
        return None
    s = str(value).strip()
    if s == "" or s.lower() == "nil":
        return None
    return s


def _as_int(value):
    s = first_scalar(value)
    if s is None:
        return None
    try:
        return int(round(float(s)))
    except ValueError:
        return None


def map_type(orca_type: str) -> str:
    return TYPE_MAP.get(orca_type, orca_type)


def collapse_bed(resolved: dict):
    """First real (non-nil, non-zero) value in textured -> hot -> cool."""
    for key in BED_PRIORITY:
        v = _as_int(resolved.get(key))
        if v:  # non-None and non-zero
            return v
    return None


def _slug(brand: str, name: str) -> str:
    raw = f"{brand}-{name}".lower()
    raw = raw.replace("+", "-plus-")  # preserve meaningful suffix, e.g. PLA+ vs PLA
    return re.sub(r"[^a-z0-9]+", "-", raw).strip("-")


def build_product(resolved: dict, base_type_range):
    """Turn a resolved Orca profile into a filaments.json product dict."""
    orca_type = first_scalar(resolved.get("filament_type")) or ""
    brand = first_scalar(resolved.get("filament_vendor")) or "Generic"
    orca_id = resolved.get("filament_id", "")  # top-level bare string
    name = first_scalar(resolved.get("_product_name")) or orca_id or "Unknown"

    nozzle = _as_int(resolved.get("nozzle_temperature"))
    nmin = _as_int(resolved.get("nozzle_temperature_range_low"))
    nmax = _as_int(resolved.get("nozzle_temperature_range_high"))

    product = {
        "id": _slug(brand, name),
        "brand": brand,
        "name": name,
        "type": map_type(orca_type),
        "nozzle": nozzle,
        "bed": collapse_bed(resolved),
        "source": "orca",
    }
    # Reconcile so nozzle_min <= nozzle <= nozzle_max ALWAYS. Orca's
    # nozzle_temperature is the real print temp; range_low/high are soft hints
    # that some upstream profiles set inconsistently (recommended outside range),
    # and unmapped types have no base range at all.
    lo, hi = nmin, nmax
    # Some upstream profiles set range_low == range_high (a single print temp
    # written into both keys). That is not a range: it carries no information, the
    # picker renders "200-200 C", and any range slider has zero travel. Discard it
    # so the base type range supplies a real one. This is what shipped `generic-eva`
    # as 200-200 when the EVA type row already had a sane 190-220.
    if lo is not None and hi is not None and lo >= hi:
        lo = hi = None
    if base_type_range is not None:
        lo = base_type_range[0] if lo is None else lo
        hi = base_type_range[1] if hi is None else hi
    if nozzle is not None:
        lo = nozzle if lo is None else min(lo, nozzle)
        hi = nozzle if hi is None else max(hi, nozzle)
    # Second gate, for the one path the first cannot reach: an UNMAPPED type has no
    # base range, so a profile carrying only a single nozzle_temperature still
    # collapses to a point. Widen it rather than emitting a degenerate range —
    # ±5 % keeps the declared temp centred and the range honestly narrow.
    if lo is not None and hi is not None and lo >= hi:
        span = max(5, int(round(lo * 0.05)))
        lo, hi = lo - span, hi + span
    # Emit an explicit range unless it exactly matches the base type range (thin).
    if lo is not None and hi is not None and (lo, hi) != base_type_range:
        product["nozzle_min"] = lo
        product["nozzle_max"] = hi
    density = resolved.get("filament_density")
    dval = first_scalar(density)
    if dval and float(dval) > 0:
        product["density"] = round(float(dval), 3)
    if orca_id:
        product["orca_id"] = orca_id
    return product


# @System / @<printer> / @base suffixes to strip from profile names for display.
_SUFFIX_RE = re.compile(r"\s*@.*$")

# Type strings HelixScreen's catalog carries that OrcaSlicer's filament library
# has no equivalent for. Mapping them to a library type is the difference
# between a correct preset and Orca's silent PLA fallback (see the spec).
#
# DELIBERATELY ABSENT — these must emit NOTHING rather than a wrong match:
#   PET, PET-GF  : polyethylene terephthalate is NOT PETG. Different polymer,
#                  different temps. The library has PET-CF but no plain PET.
#   PPS, PPS-CF  : no library equivalent; high-temp engineering material where
#   PPA          : a wrong guess risks a ruined print or a damaged hotend.
ORCA_TYPE_OVERRIDES = {
    "rPLA": "PLA",
    "rPETG": "PETG",
    "TPE": "TPU",          # Orca files COEX's TPE presets under filament_type TPU
    "TPU-95A": "TPU",
    "TPU-85A": "TPU",
    "SILK": "PLA",
    "Color-Change": "PLA",
    "PLA+": "PLA",
    "ASA+": "ASA",
    "ABS+": "ABS",
}


def _display_name(profile_name: str, brand: str) -> str:
    name = _SUFFIX_RE.sub("", profile_name).strip()
    if brand and name.lower().startswith(brand.lower() + " "):
        name = name[len(brand) + 1:]
    return name or profile_name


def build_catalog(orca_root, cfs_seed, type_ranges, library_marker="OrcaFilamentLibrary"):
    # The OrcaFilamentLibrary is self-contained (its own base/ dir). Resolve
    # inheritance ONLY within it: loading the ~30 other vendor packs clobbers
    # same-named bases (e.g. fdm_filament_pc) in the flat name->profile map and
    # corrupts inherited vendor + temperature values. See the collision test.
    lib_root = os.path.join(orca_root, "OrcaFilamentLibrary", "filament")
    load_root = lib_root if os.path.isdir(lib_root) else orca_root
    by_name = load_profiles(load_root)
    products = []
    seen = set()
    library_types = set()
    for pname, profile in by_name.items():
        if profile.get("instantiation") != "true":
            continue  # templates, not user-facing products
        if library_marker and library_marker not in profile.get("_src", ""):
            continue  # skip printer-specific profiles outside the library
        resolved = resolve_inherits(profile, by_name)
        brand = first_scalar(resolved.get("filament_vendor")) or "Generic"
        resolved["_product_name"] = _display_name(pname, brand)
        orca_type = first_scalar(resolved.get("filament_type")) or ""
        # RAW type — this is the vocabulary Orca matches on. Must NOT be
        # map_type()'d: our catalog deliberately carries finer types than Orca.
        if orca_type:
            library_types.add(orca_type)
        base_range = type_ranges.get(map_type(orca_type))
        product = build_product(resolved, base_range)
        key = (product["brand"], product["name"])
        if key in seen:
            continue  # collapse per-color duplicates
        seen.add(key)
        products.append(product)
    _merge_cfs_seed(products, cfs_seed)
    _dedupe_ids(products)
    _assert_no_degenerate_ranges(products)
    products.sort(key=lambda p: (p.get("source", ""), p["brand"].lower(), p["name"].lower(), p["id"]))
    return products, sorted(library_types)


def _merge_cfs_seed(products, cfs_seed):
    """Fold the CFS seed into the Orca products instead of appending it wholesale.

    The seed exists to carry per-material CFS codes (used to match a physical CFS
    spool by its embedded code), but it names its generics "Generic TPU" while the
    Orca library names the same material "TPU". Appended as-is, the two surface as
    duplicate picker rows with conflicting temps (the seed generics are nozzle-only).

    So: normalize each seed entry's display name to Orca convention (strip a leading
    brand prefix, e.g. "Generic TPU" -> "TPU"), then look for an Orca product of the
    same (brand, type, name). If found, move the seed's codes onto that richer Orca
    product (Orca temps win) and drop the seed copy. Seed entries with no Orca match
    (CFS-only variants like "Generic TPU 64D", or Creality "CR-*") are kept as
    standalone products, still carrying their codes.

    The same standalone-append path also carries `type` values that name a firmware
    material whitelist but have no Orca profile at all — e.g. "Generic Silk PLA"
    (type "SILK") for the AD5X stock firmware whitelist. Orca profiles silk PLA as
    plain "PLA" with a display name, so there's no Orca product to merge into; the
    seed entry is the sole source of a selectable product for that type. Regenerating
    via `make regen-filaments` re-derives assets/filaments.json from Orca + this seed,
    so hand-edits to assets/filaments.json alone would be wiped — the seed here is
    what makes the entry durable across regen.

    Despite the `cfs_seed` name, the seed file is also the durable home for
    `source: "helix-seed"` placeholder products: minimal Generic entries whose only
    job is to make a filament_database.h type reachable in the material picker,
    which builds its list from the catalog rather than from the type table. They
    carry no temps or density on purpose — every value inherits from
    filament_database.h so the two layers cannot drift. Orca has no profiles for
    these types, so they always take the standalone-append path below.
    """
    def _name_key(s):
        # Fold "PLA-Silk" == "PLA Silk", but keep "+" -- it's a meaningful suffix
        # (see _slug): "PLA+" and "PLA" are different products and must not
        # collide into the same match key.
        return re.sub(r"[^a-z0-9+]+", "", s.lower())

    # Match on (brand, name) ONLY — deliberately NOT on `type`.
    #
    # Orca's `filament_type` is an open enum that upstream mislabels for filled
    # grades: their Generic "PETG-CF" profile declares type "PETG". Including
    # `type` in the key meant the seed's correctly-typed "PETG-CF" entry missed
    # that product, appended as a second entry, and _dedupe_ids renamed it
    # `generic-petg-cf-2` — two identical Generic/PETG-CF rows in the picker, one
    # carrying the cfs code and one carrying the orca_id.
    #
    # Brand + name already identifies a product uniquely (build_catalog dedupes on
    # exactly that pair), so dropping `type` from the key costs no precision, and
    # a type disagreement now RESOLVES instead of forking.
    orca_by_key = {(p["brand"], _name_key(p["name"])): p for p in products}
    for entry in cfs_seed:
        merged = dict(entry)
        merged["name"] = _display_name(entry.get("name", ""), entry.get("brand", ""))
        merged["id"] = _slug(entry.get("brand", ""), merged["name"])
        match = orca_by_key.get((entry.get("brand"), _name_key(merged["name"])))
        if match is not None:
            seed_type = entry.get("type")
            if seed_type and match.get("type") != seed_type:
                # The seed is hand-curated against filament_database.h; Orca's
                # open-enum type is not. The seed wins, and says so out loud.
                print(f"NOTE: cfs_seed retypes {match['id']}: "
                      f"{match.get('type')!r} -> {seed_type!r}", file=sys.stderr)
                match["type"] = seed_type
            codes = match.setdefault("codes", {})
            for scheme, code in (entry.get("codes") or {}).items():
                codes.setdefault(scheme, code)  # don't clobber an existing Orca code
        else:
            products.append(merged)


def _assert_no_degenerate_ranges(products):
    """Refuse to emit a catalog containing a zero-width or inverted nozzle range.

    build_product() already reconciles Orca's ranges, but the cfs_seed path writes
    nozzle_min/nozzle_max straight through unvalidated, so this is the one gate
    every product passes regardless of source. A degenerate range is silently
    useless in the UI, which is why it survived to ship once; failing the
    regeneration is the cheapest place to catch it.
    """
    bad = [
        f"{p['id']} ({p.get('nozzle_min')}-{p.get('nozzle_max')})"
        for p in products
        if p.get("nozzle_min") is not None
        and p.get("nozzle_max") is not None
        and p["nozzle_min"] >= p["nozzle_max"]
    ]
    if bad:
        raise SystemExit(
            "ERROR: degenerate nozzle range(s) in generated catalog: " + ", ".join(sorted(bad))
        )


def _dedupe_ids(products):
    """Disambiguate duplicate `id`s in place with a `-2`, `-3`, ... suffix.

    Safety net for the union of Orca-derived products and cfs_seed: no matter
    the source, a duplicate id would silently shadow an earlier product in any
    id-keyed consumer. Logs a warning naming every colliding id.
    """
    seen_ids = {}
    collided = set()
    for product in products:
        base_id = product["id"]
        count = seen_ids.get(base_id, 0)
        if count:
            collided.add(base_id)
            new_id = f"{base_id}-{count + 1}"
            while new_id in seen_ids:
                count += 1
                new_id = f"{base_id}-{count + 1}"
            product["id"] = new_id
            seen_ids[new_id] = 1
        seen_ids[base_id] = count + 1
    if collided:
        print(f"WARNING: duplicate filament ids disambiguated: {sorted(collided)}",
              file=sys.stderr)


def _load_types(path):
    """The shipped material-type table, carried over from the existing output.

    `types` is hand-maintained inside assets/filaments.json itself (it is the
    runtime's only copy of the per-type table), so a regen must read it back
    before overwriting the file. Its nozzle ranges also seed products that do
    not carry their own.
    """
    with open(path, encoding="utf-8") as f:
        types = json.load(f).get("types")
    if not isinstance(types, list) or not types:
        raise SystemExit(f"{path} has no `types` table to carry over; refusing to write one without it")
    return types


def _type_ranges(types):
    return {t["name"]: (t["nozzle_min"], t["nozzle_max"]) for t in types}


def main(argv=None):
    ap = argparse.ArgumentParser(description="Derive filaments.json from OrcaSlicer.")
    ap.add_argument("--orca", required=True, help="OrcaSlicer resources/profiles root")
    ap.add_argument("--cfs-seed", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--orca-tag", default="unknown")
    args = ap.parse_args(argv)

    with open(args.cfs_seed, encoding="utf-8") as f:
        seed = json.load(f)
    types = _load_types(args.out)
    catalog, library_types = build_catalog(args.orca, seed, _type_ranges(types))
    doc = {
        "_attribution": ("Factual filament data derived from OrcaSlicer "
                         f"(github.com/SoftFever/OrcaSlicer, tag {args.orca_tag}, "
                         "AGPL-3.0). No OrcaSlicer profile files are shipped."),
        "orca_library_types": library_types,
        "orca_type_overrides": ORCA_TYPE_OVERRIDES,
        "types": types,
        "filaments": catalog,
    }
    with open(args.out, "w", encoding="utf-8") as f:
        json.dump(doc, f, indent=2, ensure_ascii=False)
        f.write("\n")
    print(f"Wrote {len(catalog)} filaments and {len(library_types)} Orca library types to {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
