// SPDX-License-Identifier: GPL-3.0-or-later
import { describe, expect, it } from "vitest";
import { FETCH_TIMEOUT_MS, RELEASE_WARN, compareRelease, formatTable, getJson, pickCandidate, type ReleaseRow } from "../../scripts/releaseHealth";

const row = (over: Partial<ReleaseRow> = {}): ReleaseRow => ({
  version: "1.0.3",
  active_devices: 100,
  crash_free_devices: 0.98,
  crash_rate: 0.01,
  update_failure_rate: 0.02,
  update_failures: 2,
  update_successes: 98,
  print_success_rate: 0.9,
  p95_frame_ms: 20,
  p95_rss_kb: 100000,
  ...over,
});
const warned = (c: ReleaseRow, b: ReleaseRow) => compareRelease(c, b).filter((v) => v.warn).map((v) => v.metric);

describe("compareRelease", () => {
  const W = RELEASE_WARN;
  const over = 0.0005;

  it("is quiet when the candidate matches the baseline", () => {
    expect(warned(row(), row())).toEqual([]);
  });

  it("warns on a crash-free drop only past the threshold", () => {
    const b = row({ crash_free_devices: 0.98 });
    expect(warned(row({ crash_free_devices: 0.98 - W.crashFreeDrop }), b)).not.toContain("crash-free devices");
    expect(warned(row({ crash_free_devices: 0.98 - W.crashFreeDrop - over }), b)).toContain("crash-free devices");
  });

  it("warns on a crash rate only past the ratio and past the minimum delta", () => {
    const b = row({ crash_rate: 0.02 });
    expect(warned(row({ crash_rate: 0.02 * W.crashRateRatio }), b)).not.toContain("crash rate");
    expect(warned(row({ crash_rate: 0.02 * W.crashRateRatio + over }), b)).toContain("crash rate");
    // A tiny base makes any rise a big ratio; the minimum delta keeps it quiet.
    const tiny = row({ crash_rate: 0.0001 });
    expect(warned(row({ crash_rate: 0.0001 + W.crashRateMinDelta }), tiny)).not.toContain("crash rate");
    expect(warned(row({ crash_rate: 0.0001 + W.crashRateMinDelta + 0.0001 }), tiny)).toContain("crash rate");
    // From zero the ratio is unbounded, so the delta decides alone.
    const zero = row({ crash_rate: 0 });
    expect(warned(row({ crash_rate: W.crashRateMinDelta }), zero)).not.toContain("crash rate");
    expect(warned(row({ crash_rate: W.crashRateMinDelta + 0.0001 }), zero)).toContain("crash rate");
  });

  it("warns on an update failure rise only past the threshold", () => {
    const b = row({ update_failure_rate: 0.02 });
    expect(warned(row({ update_failure_rate: 0.02 + W.updateFailureRise }), b)).not.toContain("update failure rate");
    expect(warned(row({ update_failure_rate: 0.02 + W.updateFailureRise + over }), b)).toContain("update failure rate");
  });

  it("warns on a print success drop only past the threshold", () => {
    const b = row({ print_success_rate: 0.9 });
    expect(warned(row({ print_success_rate: 0.9 - W.printSuccessDrop }), b)).not.toContain("print success rate");
    expect(warned(row({ print_success_rate: 0.9 - W.printSuccessDrop - over }), b)).toContain("print success rate");
  });

  it("warns on a frame time rise only past the threshold", () => {
    expect(warned(row({ p95_frame_ms: 20 * (1 + W.frameRise) }), row())).not.toContain("p95 frame ms");
    expect(warned(row({ p95_frame_ms: 20 * (1 + W.frameRise) + 0.1 }), row())).toContain("p95 frame ms");
  });

  it("warns on an RSS rise only past the threshold", () => {
    expect(warned(row({ p95_rss_kb: 100000 * (1 + W.rssRise) }), row())).not.toContain("p95 rss kb");
    expect(warned(row({ p95_rss_kb: 100000 * (1 + W.rssRise) + 100 }), row())).toContain("p95 rss kb");
  });

  it("never warns on a metric either side lacks", () => {
    const nulls = {
      crash_free_devices: null,
      crash_rate: null,
      update_failure_rate: null,
      print_success_rate: null,
      p95_frame_ms: null,
      p95_rss_kb: null,
    };
    expect(warned(row(nulls), row())).toEqual([]);
    expect(warned(row(), row(nulls))).toEqual([]);
    expect(compareRelease(row(nulls), row(nulls))).toHaveLength(6);
  });

  it("shows the update failure counts beside the rate", () => {
    const v = compareRelease(row({ update_failure_rate: 0, update_failures: 0, update_successes: 98 }), row())
      .find((x) => x.metric === "update failure rate")!;
    expect(v.candidate).toBe("0.00% (0/98)");
    expect(v.baseline).toBe("2.00% (2/100)");
  });
});

describe("pickCandidate", () => {
  it("picks the newest prerelease of VERSION.txt's release", () => {
    const seen = ["1.0.3", "1.0.4", "1.1.0-beta.9", "1.1.0-beta.10", "1.1.0-beta.2", "1.2.0-beta.1"];
    expect(pickCandidate("1.1.0\n", seen)).toBe("1.1.0-beta.10");
    expect(pickCandidate("1.1.0-beta.11", seen)).toBe("1.1.0-beta.10");
  });

  it("ignores other releases and stable builds, and returns null when none was seen", () => {
    expect(pickCandidate("1.1.0", ["1.0.3", "1.0.4-beta.1", "1.1.0"])).toBeNull();
    expect(pickCandidate("1.1.0", [])).toBeNull();
  });

  it("tolerates a v prefix", () => {
    expect(pickCandidate("1.1.0", ["v1.1.0-beta.3"])).toBe("v1.1.0-beta.3");
  });
});

describe("formatTable", () => {
  it("names both versions and marks warnings", () => {
    const c = row({ version: "1.1.0-beta.4", crash_free_devices: 0.9 });
    const out = formatTable(c, row(), compareRelease(c, row()));
    expect(out).toContain("1.1.0-beta.4");
    expect(out).toContain("1.0.3");
    expect(out).toMatch(/WARN.*crash-free devices/);
    // Every metric row carries the threshold it is judged by.
    for (const v of compareRelease(c, row())) {
      expect(out.split("\n").find((l) => l.startsWith(v.metric))).toContain(v.threshold);
    }
  });
});

describe("getJson", () => {
  /** A fetch that never answers, and rejects the way fetch does when its signal aborts. */
  const stalled = (async (_url: string, init: RequestInit = {}) =>
    new Promise((_resolve, reject) => {
      init.signal?.addEventListener("abort", () => reject(init.signal!.reason));
    })) as unknown as typeof fetch;

  it("gives up on a stalled connection with a one-line reason", async () => {
    await expect(getJson("https://x.test/a", undefined, stalled, 10)).rejects.toThrow("https://x.test/a timed out after 0.01s");
  });

  it("bounds every request by FETCH_TIMEOUT_MS by default", async () => {
    let signal: AbortSignal | null | undefined;
    const ok = (async (_url: string, init: RequestInit = {}) => {
      signal = init.signal;
      return new Response("{}");
    }) as unknown as typeof fetch;
    await getJson("https://x.test/b", "k", ok);
    expect(signal).toBeInstanceOf(AbortSignal);
    expect(FETCH_TIMEOUT_MS).toBe(20_000);
  });
});
