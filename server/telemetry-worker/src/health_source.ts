// SPDX-License-Identifier: GPL-3.0-or-later
// Runs the Health queries and builds the payload. The dashboard's health route and the
// feedback loop both read it here.

import { healthQueries, notApplicableFor, executeQuery, type FilterParams, type QueryConfig } from "./queries";
import { buildHealth, type HealthBlock, type HealthPayload, type HealthRowKey } from "./health";

/** The Health blocks a filter can leave unreported. */
const HEALTH_BLOCKS = ["crashes", "errors", "prints", "updates"] as const satisfies readonly HealthBlock[];

/** `today` is the UTC day (YYYY-MM-DD) the payload is built for. */
export async function fetchHealth(config: QueryConfig, today: string, filters?: FilterParams): Promise<HealthPayload> {
  // top_versions first: the per-release queries are restricted to its versions.
  const topRes = (await executeQuery(config, healthQueries(filters).top_versions)) as {
    data?: Array<{ ver: string }>;
  };
  const versions = (topRes.data ?? []).map((r) => String(r.ver));
  const queries = healthQueries(filters, versions);
  const rest = Object.keys(queries).filter((k) => k !== "top_versions") as HealthRowKey[];
  const results = await Promise.all(rest.map((k) => executeQuery(config, queries[k])));
  const rows = { top_versions: topRes.data ?? [] } as Record<HealthRowKey, unknown[]>;
  rest.forEach((k, i) => {
    rows[k] = (results[i] as { data?: unknown[] } | null)?.data ?? [];
  });
  return buildHealth(rows, today, notApplicableFor(HEALTH_BLOCKS, filters));
}
