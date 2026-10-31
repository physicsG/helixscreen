// SPDX-License-Identifier: GPL-3.0-or-later
// Attributes print outcomes to a version or printer model through each
// device's session rows. SQL lives in queries.ts (releasePrintsSql,
// deviceSessionsSql); print_outcome rows carry no printer_model and often an
// empty app_version, so the join happens here.

import { num, str, type RawRow } from "./schema";
import { compareVersions } from "./versions";

export interface PrintCounts {
  successes: number;
  total: number;
}

/** The version of prints from a device with no session before the window ends. */
export const UNKNOWN_VERSION = "unknown";

/**
 * Sums weighted print outcomes per value of `field`. `prints` are
 * releasePrintsSql rows { device_id, ver, print_successes, print_total };
 * `sessions` are deviceSessionsSql rows { device_id, model, ver }. A print
 * row's own non-empty `field` wins, else its device's latest session value,
 * else `fallback` (a device with no session).
 */
export function attributePrints(
  prints: RawRow[],
  sessions: RawRow[],
  field: "ver" | "model",
  fallback: string,
): Map<string, PrintCounts> {
  const ofDevice = new Map(sessions.map((s) => [str(s.device_id), str(s[field])]));
  const out = new Map<string, PrintCounts>();
  for (const p of prints) {
    const key = str(p[field]) || ofDevice.get(str(p.device_id)) || fallback;
    const entry = out.get(key) ?? { successes: 0, total: 0 };
    entry.successes += num(p.print_successes);
    entry.total += num(p.print_total);
    out.set(key, entry);
  }
  return out;
}

/**
 * Print outcomes per attributed version as rows { ver, print_successes,
 * print_total }, newest version first. Prints from devices with no session
 * have no version and are left out.
 */
export function printsByVersion(prints: RawRow[], sessions: RawRow[]): RawRow[] {
  return [...attributePrints(prints, sessions, "ver", UNKNOWN_VERSION)]
    .filter(([ver]) => ver !== UNKNOWN_VERSION)
    .sort(([a], [b]) => compareVersions(b, a))
    .map(([ver, c]) => ({ ver, print_successes: c.successes, print_total: c.total }));
}
