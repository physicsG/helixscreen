// SPDX-License-Identifier: GPL-3.0-or-later
// Device list and per-device page: payload types and the row folding that
// builds them. SQL lives in queries.ts. Imports only versions and schema so
// the dashboard can import these types without Worker runtime types.

import { decodeRow, num, rowsOf, str, type NotApplicable } from "./schema";
import { compareVersions } from "./versions";

const DEVICE_ID_RE = /^[0-9a-f]{64}$/;

/** A device id is 64 lowercase hex characters; anything else never reaches SQL. */
export function isDeviceId(id: string): boolean {
  return DEVICE_ID_RE.test(id);
}

export interface DeviceListRow {
  device_id: string;
  last_seen: string;
  version: string;
  platform: string;
  board: string;
  model: string;
  sessions: number;
  crashes: number;
  prints: number;
}

export interface DevicePage {
  device_id: string;
  model: string;
  platform: string;
  board: string;
  version: string;
  first_seen: string;
  last_seen: string;
  hardware: Record<string, string | number> | null;
  settings: Record<string, string | number> | null;
  versions: { version: string; first_seen: string; last_seen: string }[];
  timeline: { timestamp: string; kind: string; fields: Record<string, string | number> }[];
}

/** The device list's count columns that a filter can leave unreported. */
export type DeviceCountColumn = "crashes" | "prints";

export interface DeviceList {
  /** Distinct devices matching the filters, which can exceed the listed rows. */
  total: number;
  devices: DeviceListRow[];
  /** Per count column, the active filter its kind cannot carry: the column is unknown, not 0. */
  not_applicable: NotApplicable<DeviceCountColumn>;
}

/** Per-device counts keyed by device_id. */
function countsByDevice(res: unknown, field: string): Map<string, number> {
  const out = new Map<string, number>();
  for (const r of rowsOf(res)) out.set(str(r.device_id), num(r[field]));
  return out;
}

/**
 * Joins per-device crash and print counts onto the listed session rows; a
 * device with none shows 0. `totalRes` is the distinct-device count query,
 * which can exceed the listed rows.
 */
export function buildDeviceList(
  sessions: unknown,
  crashes: unknown,
  prints: unknown,
  totalRes?: unknown,
  notApplicable: NotApplicable<DeviceCountColumn> = { crashes: null, prints: null },
): DeviceList {
  const crashBy = countsByDevice(crashes, "crashes");
  const printBy = countsByDevice(prints, "prints");
  const devices = rowsOf(sessions).map((r) => ({
    device_id: str(r.device_id),
    last_seen: str(r.last_seen),
    version: str(r.version),
    platform: str(r.platform),
    board: str(r.board),
    model: str(r.model),
    sessions: num(r.sessions),
    crashes: crashBy.get(str(r.device_id)) ?? 0,
    prints: printBy.get(str(r.device_id)) ?? 0,
  }));
  const reported = rowsOf(totalRes)[0]?.total;
  return { total: reported === undefined ? devices.length : num(reported), devices, not_applicable: notApplicable };
}

/** The first non-empty value of `field` among decoded rows, newest first. */
function latestOf(decoded: Record<string, string | number>[], field: string): string {
  for (const d of decoded) {
    const v = d[field];
    if (typeof v === "string" && v !== "") return v;
  }
  return "";
}

/** A decoded row's fields worth showing: without device_id (the page's own) and without empty or zero values. */
function shownFields(decoded: Record<string, string | number>): Record<string, string | number> {
  return Object.fromEntries(Object.entries(decoded).filter(([k, v]) => k !== "device_id" && v !== "" && v !== 0));
}

/** Builds one device's page from its newest-first event rows; null when it has none. */
export function buildDevicePage(id: string, rows: unknown, versionRows: unknown): DevicePage | null {
  const raw = rowsOf(rows);
  if (raw.length === 0) return null;
  const sorted = [...raw].sort((a, b) => str(b.timestamp).localeCompare(str(a.timestamp)));
  const entries = sorted.map((r) => {
    const kind = str(r.index1);
    return { timestamp: str(r.timestamp), kind, decoded: decodeRow(kind, r) };
  });
  const decoded = entries.map((e) => e.decoded);
  const latestKind = (kind: string) => {
    const found = entries.find((e) => e.kind === kind);
    return found ? shownFields(found.decoded) : null;
  };

  const versions = rowsOf(versionRows)
    .map((r) => ({ version: str(r.version), first_seen: str(r.first_seen), last_seen: str(r.last_seen) }))
    .filter((v) => v.version !== "")
    .sort((a, b) => compareVersions(b.version, a.version));

  const stamps = [...entries.map((e) => e.timestamp), ...versions.map((v) => v.first_seen)].filter(Boolean);
  const firstSeen = stamps.reduce((min, t) => (t < min ? t : min), stamps[0] ?? "");

  return {
    device_id: id,
    model: latestOf(decoded, "printer_model"),
    platform: latestOf(decoded, "app_platform"),
    board: latestOf(decoded, "app_board"),
    version: latestOf(decoded, "app_version"),
    first_seen: firstSeen,
    last_seen: entries[0].timestamp,
    hardware: latestKind("hardware_profile"),
    settings: latestKind("settings_snapshot"),
    versions,
    timeline: entries.map((e) => ({
      timestamp: e.timestamp,
      kind: e.kind,
      fields: shownFields(e.decoded),
    })),
  };
}
