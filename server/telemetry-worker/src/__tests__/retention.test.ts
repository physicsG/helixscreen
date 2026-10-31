// SPDX-License-Identifier: GPL-3.0-or-later
import { describe, expect, it } from "vitest";
import { buildRetention } from "../retention";

const TODAY = "2026-10-15";

function dayOffset(offset: number): string {
  const d = new Date(Date.parse(`${TODAY}T00:00:00Z`) + offset * 86400000);
  return d.toISOString().slice(0, 10);
}

describe("buildRetention", () => {
  it("returns oldest-first cohorts, the oldest labelled before window", () => {
    const cohorts = buildRetention([], TODAY);
    expect(cohorts).toHaveLength(13);
    expect(cohorts[0].label).toBe("before window");
    expect(cohorts[12].start).toBe(dayOffset(-7));
    expect(cohorts[11].start).toBe(dayOffset(-14));
  });

  it("gives empty cohorts size 0 and all-zero survival", () => {
    for (const c of buildRetention([], TODAY)) {
      expect(c.size).toBe(0);
      expect(c.survival).toHaveLength(13);
      expect(c.survival.filter((s) => s === 0)).toHaveLength(c.survival.filter((s) => s !== null).length);
    }
  });

  it("leaves weeks that have not begun null", () => {
    const cohorts = buildRetention([{ first_day: dayOffset(-3), last_day: TODAY }], TODAY);
    const newest = cohorts[12];
    expect(newest.survival[0]).toBe(1);
    expect(newest.survival.slice(1).every((s) => s === null)).toBe(true);
    cohorts.forEach((c, i) => {
      expect(c.survival.filter((s) => s !== null)).toHaveLength(13 - i);
    });
  });

  it("counts a device seen days 0 to 30 in weeks 0..4 but not week 5", () => {
    const first = dayOffset(-70);
    const last = dayOffset(-40);
    const cohorts = buildRetention([{ first_day: first, last_day: last }], TODAY);
    const cohort = cohorts.find((c) => c.size === 1)!;
    expect(cohort.start).toBe(first);
    expect(cohort.survival.slice(0, 5)).toEqual([1, 1, 1, 1, 1]);
    expect(cohort.survival[5]).toBe(0);
  });

  it("splits cohorts at exactly 7 days", () => {
    const cohorts = buildRetention(
      [
        { first_day: dayOffset(-7), last_day: TODAY },
        { first_day: dayOffset(-8), last_day: TODAY },
        { first_day: dayOffset(-1), last_day: TODAY },
      ],
      TODAY,
    );
    expect(cohorts[12].size).toBe(2);
    expect(cohorts[11].size).toBe(1);
  });

  it("puts devices that predate the window in the oldest cohort", () => {
    const cohorts = buildRetention([{ first_day: dayOffset(-89), last_day: TODAY }], TODAY);
    expect(cohorts[0].size).toBe(1);
    expect(cohorts[0].survival[0]).toBe(1);
  });

  it("reports survival as a share of the cohort", () => {
    const first = dayOffset(-20);
    const cohorts = buildRetention(
      [
        { first_day: first, last_day: dayOffset(-20) },
        { first_day: first, last_day: TODAY },
      ],
      TODAY,
    );
    const cohort = cohorts.find((c) => c.size === 2)!;
    expect(cohort.survival[0]).toBe(1);
    expect(cohort.survival[1]).toBe(0.5);
  });
});
