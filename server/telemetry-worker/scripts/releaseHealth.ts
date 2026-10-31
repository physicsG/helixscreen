// SPDX-License-Identifier: GPL-3.0-or-later
// Compare the newest beta of VERSION.txt's release against the stable release in telemetry.
//
//   (cd server/telemetry-worker && npx vite-node scripts/releaseHealth.ts [base-url])
//
// Reads ../../VERSION.txt, the stable manifest's version, the versions telemetry has
// seen (adoption endpoint, which lists every version) and the releases endpoint for
// the candidate and stable (range=14d), then prints a table, a WARN line per metric that trails
// stable and a note when the candidate has few devices. Every outcome exits 0, with a
// one-line reason when there is nothing to compare. RELEASE_HEALTH_FIXTURE is a JSON
// file path or JSON text of {"stable": "1.0.3", "seen": ["1.1.0-beta.1"], "versions":
// [<release rows>]} that replaces the network; "seen" defaults to the rows' versions.

import { existsSync, readFileSync } from "node:fs";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { adminKeyFromEnvOrFile } from "./adminKey";
import { compareVersions } from "../src/versions";

const DEFAULT_BASE_URL = "https://telemetry.helixscreen.org";
const STABLE_MANIFEST_URL = "https://releases.helixscreen.org/stable/manifest.json";
const RANGE = "14d";
const SMALL_SAMPLE_DEVICES = 10;

export interface ReleaseRow {
  version: string;
  active_devices: number;
  crash_free_devices: number | null;
  crash_rate: number | null;
  update_failure_rate: number | null;
  update_failures: number;
  update_successes: number;
  print_success_rate: number | null;
  p95_frame_ms: number | null;
  p95_rss_kb: number | null;
}

export interface Verdict {
  metric: string;
  candidate: string;
  baseline: string;
  threshold: string;
  warn: boolean;
}

/** Absolute for rates (fractions), relative for the frame and memory rises. */
export const RELEASE_WARN = {
  crashFreeDrop: 0.01,
  crashRateRatio: 1.5,
  crashRateMinDelta: 0.001,
  updateFailureRise: 0.05,
  printSuccessDrop: 0.03,
  frameRise: 0.2,
  rssRise: 0.15,
};

// A change warns only when it is more than the threshold; EPS keeps float error
// from turning an exact-threshold change into a warning.
const EPS = 1e-9;
const exceeds = (change: number, threshold: number) => change > threshold + EPS;

const pct = (v: number | null) => (v === null ? "n/a" : `${(v * 100).toFixed(2)}%`);
const withCounts = (r: ReleaseRow) =>
  `${pct(r.update_failure_rate)} (${r.update_failures}/${r.update_failures + r.update_successes})`;
const fixed = (v: number | null) => (v === null ? "n/a" : String(Math.round(v * 10) / 10));

/** One verdict per compared metric; a metric either side lacks never warns. */
export function compareRelease(c: ReleaseRow, b: ReleaseRow): Verdict[] {
  const both = (x: number | null, y: number | null): x is number => x !== null && y !== null;
  const out: Verdict[] = [];

  out.push({
    metric: "crash-free devices",
    candidate: pct(c.crash_free_devices),
    baseline: pct(b.crash_free_devices),
    threshold: `drop > ${RELEASE_WARN.crashFreeDrop * 100} points`,
    warn:
      both(c.crash_free_devices, b.crash_free_devices) &&
      exceeds(b.crash_free_devices! - c.crash_free_devices, RELEASE_WARN.crashFreeDrop),
  });

  const crashWarn =
    both(c.crash_rate, b.crash_rate) &&
    exceeds(c.crash_rate - b.crash_rate!, RELEASE_WARN.crashRateMinDelta) &&
    exceeds(c.crash_rate, b.crash_rate! * RELEASE_WARN.crashRateRatio);
  out.push({
    metric: "crash rate",
    candidate: pct(c.crash_rate),
    baseline: pct(b.crash_rate),
    threshold: `> ${RELEASE_WARN.crashRateRatio}x and > +${RELEASE_WARN.crashRateMinDelta * 100} points`,
    warn: crashWarn,
  });

  out.push({
    metric: "update failure rate",
    candidate: withCounts(c),
    baseline: withCounts(b),
    threshold: `rise > ${RELEASE_WARN.updateFailureRise * 100} points`,
    warn:
      both(c.update_failure_rate, b.update_failure_rate) &&
      exceeds(c.update_failure_rate - b.update_failure_rate!, RELEASE_WARN.updateFailureRise),
  });

  out.push({
    metric: "print success rate",
    candidate: pct(c.print_success_rate),
    baseline: pct(b.print_success_rate),
    threshold: `drop > ${RELEASE_WARN.printSuccessDrop * 100} points`,
    warn:
      both(c.print_success_rate, b.print_success_rate) &&
      exceeds(b.print_success_rate! - c.print_success_rate, RELEASE_WARN.printSuccessDrop),
  });

  const rise = (x: number | null, y: number | null, limit: number) =>
    both(x, y) && y! > 0 && exceeds((x - y!) / y!, limit);
  out.push({
    metric: "p95 frame ms",
    candidate: fixed(c.p95_frame_ms),
    baseline: fixed(b.p95_frame_ms),
    threshold: `rise > ${RELEASE_WARN.frameRise * 100}%`,
    warn: rise(c.p95_frame_ms, b.p95_frame_ms, RELEASE_WARN.frameRise),
  });
  out.push({
    metric: "p95 rss kb",
    candidate: fixed(c.p95_rss_kb),
    baseline: fixed(b.p95_rss_kb),
    threshold: `rise > ${RELEASE_WARN.rssRise * 100}%`,
    warn: rise(c.p95_rss_kb, b.p95_rss_kb, RELEASE_WARN.rssRise),
  });

  return out;
}

const coreOf = (v: string) => v.replace(/^v/, "").split("+")[0].split("-")[0];

/** The newest prerelease of VERSION.txt's release among the versions telemetry has seen. */
export function pickCandidate(versionTxt: string, seenVersions: string[]): string | null {
  const release = coreOf(versionTxt.trim());
  const pre = seenVersions.filter((v) => v.replace(/^v/, "").includes("-") && coreOf(v) === release);
  if (pre.length === 0) return null;
  return pre.sort(compareVersions)[pre.length - 1];
}

/** Fixed-width table of both versions' metrics, a WARN line per verdict that tripped, and a small-sample note. */
export function formatTable(c: ReleaseRow, b: ReleaseRow, verdicts: Verdict[]): string {
  const w = Math.max(7, ...verdicts.map((v) => v.metric.length));
  const cw = Math.max(c.version.length, ...verdicts.map((v) => v.candidate.length));
  const bw = Math.max(b.version.length, ...verdicts.map((v) => v.baseline.length));
  const row = (m: string, x: string, y: string, t: string) =>
    `${m.padEnd(w)}  ${x.padEnd(cw)}  ${y.padEnd(bw)}  ${t}`.trimEnd();
  const lines = [
    row("metric", c.version, b.version, "warns on"),
    ...verdicts.map((v) => row(v.metric, v.candidate, v.baseline, v.threshold)),
    row("devices", String(c.active_devices), String(b.active_devices), ""),
  ];
  for (const v of verdicts.filter((x) => x.warn)) {
    lines.push(`WARN ${v.metric}: ${c.version} ${v.candidate} vs ${b.version} ${v.baseline} (warns on ${v.threshold})`);
  }
  if (c.active_devices < SMALL_SAMPLE_DEVICES) {
    lines.push(`Note: ${c.version} has only ${c.active_devices} devices in ${RANGE}; read the numbers as a hint.`);
  }
  return lines.join("\n");
}

interface Fixture {
  stable: string;
  seen?: string[];
  versions: ReleaseRow[];
}

function loadFixture(spec: string): Fixture {
  return JSON.parse(existsSync(spec) ? readFileSync(spec, "utf8") : spec) as Fixture;
}

/** A stalled connection would otherwise hold the release gate for minutes. */
export const FETCH_TIMEOUT_MS = 20_000;

export async function getJson(url: string, key?: string, fetchImpl: typeof fetch = fetch, timeoutMs = FETCH_TIMEOUT_MS): Promise<unknown> {
  let res: Response;
  try {
    res = await fetchImpl(url, { headers: key ? { "X-API-Key": key } : {}, signal: AbortSignal.timeout(timeoutMs) });
  } catch (err) {
    if (err instanceof Error && err.name === "TimeoutError") throw new Error(`${url} timed out after ${timeoutMs / 1000}s`);
    throw err;
  }
  if (!res.ok) throw new Error(`${url} returned ${res.status}`);
  return res.json();
}

async function run(): Promise<string> {
  const root = resolve(dirname(fileURLToPath(import.meta.url)), "../../..");
  const versionTxt = readFileSync(resolve(root, "VERSION.txt"), "utf8").trim();

  let stable: string;
  let seenVersions: () => Promise<string[]>;
  let rowsFor: (versions: string[]) => Promise<ReleaseRow[]>;
  const fixtureSpec = process.env.RELEASE_HEALTH_FIXTURE;
  if (fixtureSpec) {
    const fx = loadFixture(fixtureSpec);
    stable = fx.stable;
    seenVersions = async () => fx.seen ?? fx.versions.map((r) => r.version);
    rowsFor = async (versions) => fx.versions.filter((r) => versions.includes(r.version));
  } else {
    const key = adminKeyFromEnvOrFile();
    if (!key) return "release-health: no admin key (HELIX_TELEMETRY_ADMIN_KEY or .env.telemetry); skipped";
    const base = (process.argv[2] ?? DEFAULT_BASE_URL).replace(/\/$/, "");
    stable = String(((await getJson(STABLE_MANIFEST_URL)) as { version?: string }).version ?? "");
    seenVersions = async () =>
      (((await getJson(`${base}/v1/dashboard/adoption?range=${RANGE}`, key)) as { versions?: Array<{ name: string }> }).versions ?? []).map(
        (v) => v.name,
      );
    rowsFor = async (versions) => {
      const qs = `range=${RANGE}&versions=${versions.map(encodeURIComponent).join(",")}`;
      return ((await getJson(`${base}/v1/dashboard/releases?${qs}`, key)) as { versions?: ReleaseRow[] }).versions ?? [];
    };
  }
  if (!stable) return "release-health: the stable manifest names no version; skipped";

  const candidate = pickCandidate(versionTxt, await seenVersions());
  if (!candidate) return `release-health: no ${versionTxt} prerelease in telemetry over ${RANGE}; nothing to compare`;

  const rows = await rowsFor([candidate, stable]);
  const c = rows.find((r) => r.version === candidate);
  const b = rows.find((r) => compareVersions(r.version, stable) === 0);
  if (!c || !b) return `release-health: no telemetry for ${!c ? candidate : stable} over ${RANGE}; nothing to compare`;
  return formatTable(c, b, compareRelease(c, b));
}

// vitest imports this file for the pure functions above; it must not run the check.
if (!process.env.VITEST) {
  run()
    .then((out) => console.log(out))
    .catch((err) => console.log(`release-health: ${err instanceof Error ? err.message : err}; skipped`))
    .finally(() => process.exit(0));
}
