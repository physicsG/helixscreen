// SPDX-License-Identifier: GPL-3.0-or-later
// Compare one UTC day of raw R2 batches against what Analytics Engine kept.
//
//   HELIX_TELEMETRY_ADMIN_KEY comes from the environment, else from the repo
//   root's .env.telemetry (KEY=VALUE lines).
//   (cd server/telemetry-worker && npx vite-node scripts/reconcile.ts 2026-10-12)
//
// R2 holds every accepted batch; AE sheds writes past its per-invocation
// ceiling and samples under load. For each AE kind (index1) the table shows the
// points the R2 events map to next to the rows AE holds, and kept% = how much
// of R2 survived into AE (ae_weighted / r2_points).

import { adminKeyFromEnvOrFile, parseEnvValue } from "./adminKey";
import { mapEventToDataPoints } from "../src/analytics";
import { isIsoDate } from "../src/queries";

export { parseEnvValue };

const BASE_URL = "https://telemetry.helixscreen.org";
const FETCH_CONCURRENCY = 16;

export interface AeKindRow {
  kind: string;
  rows: number;
  weighted: number;
  devices: number;
}

export interface ReconcileRow {
  kind: string;
  r2_points: number;
  ae_rows: number;
  ae_weighted: number;
  "kept%": string;
}

/**
 * Count the Analytics Engine points R2 batches map to, keyed by AE kind
 * (index1). A batch is either an array of events or {events: [...]}.
 */
export function countR2Points(batches: unknown[]): Record<string, number> {
  const counts: Record<string, number> = {};
  for (const batch of batches) {
    const events = Array.isArray(batch)
      ? batch
      : batch && typeof batch === "object" && Array.isArray((batch as { events?: unknown }).events)
        ? (batch as { events: unknown[] }).events
        : [];
    for (const event of events) {
      if (!event || typeof event !== "object" || Array.isArray(event)) continue;
      for (const point of mapEventToDataPoints(event as Record<string, unknown>)) {
        const kind = point.indexes?.[0] ?? "";
        counts[kind] = (counts[kind] ?? 0) + 1;
      }
    }
  }
  return counts;
}

/**
 * One row per kind: kinds present in R2 first (alphabetical), then kinds only
 * AE has (alphabetical). kept% is "n/a" when R2 has no points to compare to.
 */
export function buildReconcileTable(
  r2Points: Record<string, number>,
  aeKinds: AeKindRow[],
): ReconcileRow[] {
  const ae = new Map(aeKinds.map((r) => [r.kind, r]));
  const r2Kinds = Object.keys(r2Points).sort();
  const aeOnly = aeKinds.map((r) => r.kind).filter((k) => !(k in r2Points)).sort();
  return [...r2Kinds, ...aeOnly].map((kind) => {
    const r2 = r2Points[kind] ?? 0;
    const row = ae.get(kind);
    const weighted = row?.weighted ?? 0;
    return {
      kind,
      r2_points: r2,
      ae_rows: row?.rows ?? 0,
      ae_weighted: weighted,
      "kept%": r2 > 0 ? ((weighted / r2) * 100).toFixed(1) : "n/a",
    };
  });
}

async function adminGet(path: string, adminKey: string): Promise<Response> {
  const res = await fetch(`${BASE_URL}${path}`, { headers: { "X-API-Key": adminKey } });
  if (!res.ok) {
    throw new Error(`GET ${path} -> ${res.status} ${await res.text()}`);
  }
  return res;
}

async function listDayKeys(date: string, adminKey: string): Promise<string[]> {
  const prefix = `events/${date.replaceAll("-", "/")}/`;
  const keys: string[] = [];
  let cursor: string | undefined;
  do {
    const qs = new URLSearchParams({ prefix });
    if (cursor) qs.set("cursor", cursor);
    const page = (await (await adminGet(`/v1/events/list?${qs}`, adminKey)).json()) as {
      keys: Array<{ key: string }>;
      truncated: boolean;
      cursor?: string;
    };
    keys.push(...page.keys.map((k) => k.key));
    cursor = page.truncated ? page.cursor : undefined;
  } while (cursor);
  return keys;
}

async function fetchBatches(keys: string[], adminKey: string): Promise<unknown[]> {
  const batches: unknown[] = [];
  let next = 0;
  const worker = async () => {
    while (next < keys.length) {
      const key = keys[next++];
      const res = await adminGet(`/v1/events/get?key=${encodeURIComponent(key)}`, adminKey);
      batches.push(await res.json());
    }
  };
  await Promise.all(Array.from({ length: Math.min(FETCH_CONCURRENCY, keys.length) }, worker));
  return batches;
}

async function main(): Promise<void> {
  const date = process.argv[2] ?? "";
  if (!isIsoDate(date)) {
    console.error("usage: vite-node scripts/reconcile.ts YYYY-MM-DD");
    process.exit(2);
  }
  const adminKey = adminKeyFromEnvOrFile();
  if (!adminKey) {
    console.error("HELIX_TELEMETRY_ADMIN_KEY is not set in the environment or .env.telemetry");
    process.exit(2);
  }

  const keys = await listDayKeys(date, adminKey);
  console.error(`${date}: ${keys.length} R2 batches`);
  const batches = await fetchBatches(keys, adminKey);
  const ae = (await (await adminGet(`/v1/admin/ae-counts?date=${date}`, adminKey)).json()) as {
    kinds: AeKindRow[];
  };
  console.table(buildReconcileTable(countR2Points(batches), ae.kinds));
}

// vitest imports this file for the pure functions above; it must not run main().
if (!process.env.VITEST) {
  main().catch((err) => {
    console.error(err instanceof Error ? err.message : err);
    process.exit(1);
  });
}
