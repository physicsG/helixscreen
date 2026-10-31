// SPDX-License-Identifier: GPL-3.0-or-later
import { readFileSync } from "node:fs";
import { resolve } from "node:path";
import { describe, expect, it } from "vitest";
import {
  FILTERED_ENDPOINTS,
  UNFILTERED_ENDPOINTS,
  filterSets,
  DEFAULT_DELAY_MS,
  formatSmokeTable,
  parseSmokeArgs,
  passed,
  pickMatrix,
  releasesVersionsPath,
  shouldRetry,
} from "../../scripts/smoke";

describe("formatSmokeTable", () => {
  const rows = [
    { label: "health 7d", path: "/v1/dashboard/health?range=7d", expected: 200, status: 200 },
    { label: "device/xyz is 400", path: "/v1/dashboard/device/xyz", expected: 400, status: 200 },
  ];

  it("marks each row PASS or FAIL against its expected status", () => {
    expect(passed(rows[0])).toBe(true);
    expect(passed(rows[1])).toBe(false);
    const lines = formatSmokeTable(rows).split("\n");
    expect(lines[0]).toMatch(/^PASS +200 +health 7d/);
    expect(lines[1]).toMatch(/^FAIL +200 +device\/xyz is 400/);
  });

  it("counts failures in the summary and omits the FAILED note when all pass", () => {
    expect(formatSmokeTable(rows)).toContain("1/2 passed, 1 FAILED");
    expect(formatSmokeTable([rows[0]])).toMatch(/1\/1 passed$/);
  });

  it("shows a transport error string as a failure", () => {
    const r = { label: "x", path: "/p", expected: 200, status: "fetch failed" };
    expect(passed(r)).toBe(false);
    expect(formatSmokeTable([r])).toContain("fetch failed");
  });
});

describe("pickMatrix / filterSets", () => {
  it("takes the newest version by semver and the model with the most devices", () => {
    const m = pickMatrix({
      versions: [{ name: "1.0.4" }, { name: "1.0.10" }, { name: "0.99.118" }],
      printer_models: [
        { name: "A", count: 3 },
        { name: "Voron 2.4", count: 9 },
      ],
    });
    expect(m).toEqual({ newestVersion: "1.0.10", newestTwo: ["1.0.10", "1.0.4"], topModel: "Voron 2.4" });
  });

  it("names the two newest versions in the releases request, or skips it with none", () => {
    expect(releasesVersionsPath(pickMatrix({ versions: [{ name: "1.0.4" }, { name: "1.0.10" }, { name: "0.99.118" }] }))).toBe(
      "/v1/dashboard/releases?range=30d&versions=1.0.10,1.0.4",
    );
    expect(releasesVersionsPath(pickMatrix({ versions: [{ name: "1.1.0-beta.8" }] }))).toBe(
      "/v1/dashboard/releases?range=30d&versions=1.1.0-beta.8",
    );
    expect(releasesVersionsPath(pickMatrix({}))).toBeNull();
  });

  it("encodes the values and drops filters it has no value for", () => {
    const sets = filterSets({ newestVersion: "1.1.0-beta.8", topModel: "Voron 2.4" });
    expect(sets.map((s) => s.qs)).toEqual([
      "",
      "platform=pi",
      "version=1.1.0-beta.8",
      "model=Voron%202.4",
      "board=rpi4",
    ]);
    expect(filterSets({}).map((s) => s.qs)).toEqual(["", "platform=pi", "board=rpi4"]);
  });
});

describe("smoke endpoint coverage", () => {
  it("lists every /v1/dashboard/ route index.ts serves", () => {
    const src = readFileSync(resolve(__dirname, "../index.ts"), "utf8");
    const served = new Set<string>();
    for (const m of src.matchAll(/url\.pathname === "\/v1\/dashboard\/([a-z\/-]+)"/g)) served.add(m[1]);
    expect(served.size).toBeGreaterThan(10);
    const covered = new Set([...FILTERED_ENDPOINTS, ...UNFILTERED_ENDPOINTS, "device/", "reports/"]);
    for (const route of served) expect(covered.has(route), route).toBe(true);
    expect(src).toContain('startsWith("/v1/dashboard/device/")');
    expect(src).toContain('startsWith("/v1/dashboard/reports/")');
  });
});

describe("parseSmokeArgs", () => {
  it("defaults to the production worker and the default delay", () => {
    expect(parseSmokeArgs([])).toEqual({ base: "https://telemetry.helixscreen.org", delayMs: DEFAULT_DELAY_MS });
    expect(DEFAULT_DELAY_MS).toBe(300);
  });

  it("takes a base URL, trailing slash dropped, and --delay-ms in either order", () => {
    expect(parseSmokeArgs(["http://localhost:8787/", "--delay-ms", "50"])).toEqual({ base: "http://localhost:8787", delayMs: 50 });
    expect(parseSmokeArgs(["--delay-ms", "0", "http://x"])).toEqual({ base: "http://x", delayMs: 0 });
  });

  it.each([[["--delay-ms"]], [["--delay-ms", "-1"]], [["--delay-ms", "fast"]], [["--delay-ms", "1.5"]], [["--bogus"]]])(
    "rejects %j",
    (argv) => {
      expect(() => parseSmokeArgs(argv)).toThrow();
    },
  );
});

describe("shouldRetry", () => {
  it("retries a 5xx the request did not expect", () => {
    expect(shouldRetry(502, 200)).toBe(true);
    expect(shouldRetry(500, 200)).toBe(true);
    expect(shouldRetry(599, 404)).toBe(true);
  });

  it("does not retry a pass, a 4xx or a transport error", () => {
    expect(shouldRetry(200, 200)).toBe(false);
    expect(shouldRetry(404, 200)).toBe(false);
    expect(shouldRetry(600, 200)).toBe(false);
    expect(shouldRetry("fetch failed", 200)).toBe(false);
  });
});

describe("formatSmokeTable retries", () => {
  it("reports a pass on the retry as PASS (retry)", () => {
    const r = { label: "health 7d", path: "/p", expected: 200, status: 200, retried: true };
    expect(formatSmokeTable([r]).split("\n")[0]).toMatch(/^PASS \(retry\) +200 +health 7d/);
  });

  it("reports a failed retry as FAIL", () => {
    const r = { label: "health 7d", path: "/p", expected: 200, status: 502, retried: true };
    expect(formatSmokeTable([r]).split("\n")[0]).toMatch(/^FAIL +502/);
  });
});
