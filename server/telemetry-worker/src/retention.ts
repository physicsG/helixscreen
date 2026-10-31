// SPDX-License-Identifier: GPL-3.0-or-later
// Weekly retention cohorts built from each device's first and last session
// day. SQL lives in queries.ts. Imports nothing from the Worker runtime so the
// dashboard can import these types.

export interface RetentionCohort {
  start: string;
  label: string;
  size: number;
  /**
   * survival[k] is the share of the cohort still seen on or after week k, or
   * null when week k has not begun yet (the cohort's start plus 7k days is not
   * before today). A cohort counted k weeks back has k + 1 non-null entries.
   */
  survival: (number | null)[];
}

const DAY_MS = 86400000;
const WEEK_DAYS = 7;

function dayNumber(iso: string): number {
  return Math.floor(Date.parse(`${iso.slice(0, 10)}T00:00:00Z`) / DAY_MS);
}

function dayString(n: number): string {
  return new Date(n * DAY_MS).toISOString().slice(0, 10);
}

/**
 * Cohort k (counted back from `today`) covers [today - 7(k+1), today - 7k);
 * the newest cohort (k = 0) also takes devices first seen today.
 * The oldest cohort also takes every device first seen before its start, since
 * those were already running when the window opened. Cohorts are oldest first.
 */
export function buildRetention(
  rows: { first_day: string; last_day: string }[],
  today: string,
  weeks = 13,
): RetentionCohort[] {
  const todayN = dayNumber(today);
  const members: number[][] = Array.from({ length: weeks }, () => []);
  for (const row of rows) {
    const first = dayNumber(row.first_day);
    const last = dayNumber(row.last_day);
    if (Number.isNaN(first) || Number.isNaN(last) || first > todayN) continue;
    const k = Math.max(0, Math.min(weeks - 1, Math.floor((todayN - first - 1) / WEEK_DAYS)));
    members[weeks - 1 - k].push(last);
  }

  return members.map((lasts, index) => {
    const k = weeks - 1 - index;
    const startN = todayN - WEEK_DAYS * (k + 1);
    const survival = Array.from({ length: weeks }, (_, week) =>
      week > k ? null : lasts.length === 0 ? 0 : lasts.filter((l) => Math.max(l, startN) >= startN + WEEK_DAYS * week).length / lasts.length,
    );
    return {
      start: dayString(startN),
      label: index === 0 ? "before window" : dayString(startN),
      size: lasts.length,
      survival,
    };
  });
}
