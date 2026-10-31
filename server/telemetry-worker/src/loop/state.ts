// SPDX-License-Identifier: GPL-3.0-or-later
// What the loop remembers between runs, kept as one JSON object in R2.

import type { ObjectStore } from './types'
import { ALERT_REPEAT_DAYS } from './thresholds'

const STATE_KEY = 'loop/state.json'
const DAY_MS = 86_400_000

export interface LoopState {
  /** fingerprint -> ISO date the alert was emailed */
  alerts: Record<string, string>
  /** fingerprint -> ISO week of the last comment on its issue */
  issueComments: Record<string, string>
}

function emptyState(): LoopState {
  return { alerts: {}, issueComments: {} }
}

function stringMap(v: unknown): Record<string, string> {
  const out: Record<string, string> = {}
  if (v && typeof v === 'object') {
    for (const [k, val] of Object.entries(v)) if (typeof val === 'string') out[k] = val
  }
  return out
}

export async function loadState(store: ObjectStore): Promise<LoopState> {
  const raw = await store.get(STATE_KEY)
  if (raw === null) return emptyState()
  try {
    const parsed = JSON.parse(raw)
    return { alerts: stringMap(parsed?.alerts), issueComments: stringMap(parsed?.issueComments) }
  } catch (err) {
    console.warn(`[loop] ${new Date().toISOString()} unparsable ${STATE_KEY}, starting empty: ${err}`)
    return emptyState()
  }
}

export async function saveState(store: ObjectStore, state: LoopState): Promise<void> {
  await store.put(STATE_KEY, JSON.stringify(state))
}

/** True when the fingerprint was never alerted, or last alerted more than ALERT_REPEAT_DAYS before `today` (YYYY-MM-DD). */
export function alertDue(state: LoopState, fp: string, today: string): boolean {
  const sent = state.alerts[fp]
  if (!sent) return true
  const elapsed = (Date.parse(today) - Date.parse(sent)) / DAY_MS
  return !(elapsed <= ALERT_REPEAT_DAYS)
}
