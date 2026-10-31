// SPDX-License-Identifier: GPL-3.0-or-later
// The weekly report snapshot stored in R2 and served to the dashboard. Types only,
// plus the storage key, so the dashboard can import it without Worker runtime types.

import type { HealthPayload } from '../health'
import type { Finding } from './findings'
import type { IssueOutcome } from './issues'

export interface WeeklyReport {
  /** The ISO week the report covers, e.g. 2026-W42. */
  week: string
  generated_at: string
  health: HealthPayload
  findings: Finding[]
  issues: IssueOutcome[]
  /** Why issues were not synced this week; absent when they were. */
  issues_not_synced?: string
}

export const REPORT_PREFIX = 'reports/'

export const reportKey = (week: string): string => `${REPORT_PREFIX}${week}.json`

/** A week key as `reportKey` writes it, e.g. 2026-W42. */
export const WEEK_PATTERN = /^\d{4}-W\d{2}$/

/** The weeks held in a listing of REPORT_PREFIX, newest first; keys that are not reports are ignored. */
export function reportWeeks(keys: string[]): string[] {
  return keys
    .filter((k) => k.startsWith(REPORT_PREFIX) && k.endsWith('.json'))
    .map((k) => k.slice(REPORT_PREFIX.length, -'.json'.length))
    .filter((w) => WEEK_PATTERN.test(w))
    .sort()
    .reverse()
}
