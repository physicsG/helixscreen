// SPDX-License-Identifier: GPL-3.0-or-later
// Hit every dashboard endpoint across the filter matrix and report PASS/FAIL.
//
//   (cd server/telemetry-worker && npx vite-node scripts/smoke.ts [base-url] [--delay-ms N])
//
// The base URL defaults to the production worker. The admin key comes from
// HELIX_TELEMETRY_ADMIN_KEY or the repo root's .env.telemetry. Requests run one
// at a time, --delay-ms apart (default 300), because a full run back to back
// trips Analytics Engine's rate limits; a 5xx is retried once after 5 s. Exits
// 1 when any request returns an unexpected status.

import { adminKeyFromEnvOrFile } from "./adminKey";
import { compareVersions } from "../src/versions";

const DEFAULT_BASE_URL = "https://telemetry.helixscreen.org";
const RANGES = ["7d", "30d", "90d"];
const ZERO_DEVICE_ID = "0".repeat(64);
export const DEFAULT_DELAY_MS = 300;
const RETRY_AFTER_MS = 5000;

/** Endpoints that take the range and filter parameters, in /v1/dashboard/. */
export const FILTERED_ENDPOINTS = [
  "overview",
  "adoption",
  "prints",
  "crashes",
  "crash-list",
  "memory",
  "memory-warnings",
  "hardware",
  "engagement",
  "reliability",
  "stability",
  "releases",
  "performance",
  "features",
  "ux",
  "health",
  "devices",
  "fleet/retention",
];

/** Endpoints in /v1/dashboard/ that ignore the range and filter parameters. */
export const UNFILTERED_ENDPOINTS = ["reports"];

export interface SmokeResult {
  label: string;
  path: string;
  expected: number;
  status: number | string;
  /** The status is the second attempt's, after a 5xx. */
  retried?: boolean;
}

export function passed(r: SmokeResult): boolean {
  return r.status === r.expected;
}

/** A 5xx the request did not expect is retried once; Analytics Engine returns 502 under load. */
export function shouldRetry(status: number | string, expected: number): boolean {
  return typeof status === "number" && status !== expected && status >= 500 && status <= 599;
}

/** The base URL (positional, trailing slash dropped) and --delay-ms. Throws on anything else. */
export function parseSmokeArgs(argv: string[]): { base: string; delayMs: number } {
  let base = DEFAULT_BASE_URL;
  let delayMs = DEFAULT_DELAY_MS;
  for (let i = 0; i < argv.length; i++) {
    const arg = argv[i];
    if (arg === "--delay-ms") {
      const value = argv[++i];
      if (value === undefined || !/^\d+$/.test(value)) throw new Error(`--delay-ms takes a whole number of milliseconds, got ${value}`);
      delayMs = Number(value);
    } else if (arg.startsWith("-")) {
      throw new Error(`unknown option ${arg}`);
    } else {
      base = arg;
    }
  }
  return { base: base.replace(/\/$/, ""), delayMs };
}

/** Fixed-width PASS/FAIL table ending in a count of passes and failures. */
export function formatSmokeTable(results: SmokeResult[]): string {
  const labelW = Math.max(5, ...results.map((r) => r.label.length));
  const mark = (r: SmokeResult) => (!passed(r) ? "FAIL" : r.retried ? "PASS (retry)" : "PASS");
  const lines = results.map(
    (r) => `${mark(r).padEnd(12)}  ${String(r.status).padEnd(5)} ${r.label.padEnd(labelW)}  ${r.path}`,
  );
  const failed = results.filter((r) => !passed(r)).length;
  lines.push("", `${results.length - failed}/${results.length} passed${failed ? `, ${failed} FAILED` : ""}`);
  return lines.join("\n");
}

export interface Matrix {
  newestVersion?: string;
  /** The two newest versions, newest first, for the explicit-versions releases request. */
  newestTwo?: string[];
  topModel?: string;
}

/** Filter sets applied to every endpoint: none, then one per filter key. */
export function filterSets(m: Matrix): Array<{ label: string; qs: string }> {
  const sets = [{ label: "no filter", qs: "" }, { label: "platform=pi", qs: "platform=pi" }];
  if (m.newestVersion) sets.push({ label: "version", qs: `version=${encodeURIComponent(m.newestVersion)}` });
  if (m.topModel) sets.push({ label: "model", qs: `model=${encodeURIComponent(m.topModel)}` });
  sets.push({ label: "board=rpi4", qs: "board=rpi4" });
  return sets;
}

export function pickMatrix(adoption: {
  versions?: Array<{ name: string }>;
  printer_models?: Array<{ name: string; count: number }>;
}): Matrix {
  const versions = (adoption.versions ?? []).map((v) => v.name).sort(compareVersions);
  const models = [...(adoption.printer_models ?? [])].sort((a, b) => b.count - a.count);
  return {
    newestVersion: versions[versions.length - 1],
    newestTwo: versions.slice(-2).reverse(),
    topModel: models[0]?.name,
  };
}

/** The releases request that names its versions, or null when adoption listed none. */
export function releasesVersionsPath(m: Matrix): string | null {
  if (!m.newestTwo?.length) return null;
  return `/v1/dashboard/releases?range=30d&versions=${m.newestTwo.map(encodeURIComponent).join(",")}`;
}

async function get(base: string, path: string, key: string): Promise<{ status: number; body: unknown }> {
  const res = await fetch(`${base}${path}`, { headers: { "X-API-Key": key } });
  let body: unknown = null;
  try {
    body = await res.json();
  } catch {
    // A non-JSON body only matters for the status.
  }
  return { status: res.status, body };
}

const sleep = (ms: number) => new Promise((resolve) => setTimeout(resolve, ms));

async function main(): Promise<void> {
  const { base, delayMs } = parseSmokeArgs(process.argv.slice(2));
  const key = adminKeyFromEnvOrFile();
  if (!key) {
    console.error("HELIX_TELEMETRY_ADMIN_KEY is not set in the environment or .env.telemetry");
    process.exit(2);
  }
  const results: SmokeResult[] = [];
  const check = async (label: string, path: string, expected = 200) => {
    if (results.length > 0) await sleep(delayMs);
    try {
      let res = await get(base, path, key);
      const retried = shouldRetry(res.status, expected);
      if (retried) {
        await sleep(RETRY_AFTER_MS);
        res = await get(base, path, key);
      }
      results.push({ label, path, expected, status: res.status, ...(retried ? { retried } : {}) });
      return res.body;
    } catch (err) {
      results.push({ label, path, expected, status: err instanceof Error ? err.message : "error" });
      return null;
    }
  };

  const adoption = (await check("matrix source", "/v1/dashboard/adoption?range=30d")) as Parameters<
    typeof pickMatrix
  >[0] | null;
  const matrix = pickMatrix(adoption ?? {});
  for (const endpoint of FILTERED_ENDPOINTS) {
    for (const range of RANGES) {
      for (const set of filterSets(matrix)) {
        const qs = [`range=${range}`, set.qs].filter(Boolean).join("&");
        await check(`${endpoint} ${range} ${set.label}`, `/v1/dashboard/${endpoint}?${qs}`);
      }
    }
  }

  const reports = (await check("reports list", "/v1/dashboard/reports")) as { weeks?: string[] } | null;
  const newestWeek = reports?.weeks?.[0];
  if (newestWeek) await check("reports/<newest week>", `/v1/dashboard/reports/${newestWeek}`);
  await check("reports/bad week is 400", "/v1/dashboard/reports/latest", 400);

  const releasesPath = releasesVersionsPath(matrix);
  if (releasesPath) await check("releases two newest versions", releasesPath);

  const devices = (await check("devices (for id)", "/v1/dashboard/devices?range=90d")) as {
    devices?: Array<{ device_id: string }>;
  } | null;
  const firstId = devices?.devices?.[0]?.device_id;
  if (firstId) await check("device/<first id>", `/v1/dashboard/device/${firstId}`);
  else results.push({ label: "device/<first id>", path: "(devices list is empty)", expected: 200, status: "none" });
  await check("device/<64 zeros> is 404", `/v1/dashboard/device/${ZERO_DEVICE_ID}`, 404);
  await check("device/xyz is 400", "/v1/dashboard/device/xyz", 400);

  console.log(formatSmokeTable(results));
  if (results.some((r) => !passed(r))) process.exit(1);
}

// vitest imports this file for the pure functions above; it must not run main().
if (!process.env.VITEST) {
  main().catch((err) => {
    console.error(err instanceof Error ? err.message : err);
    process.exit(1);
  });
}
