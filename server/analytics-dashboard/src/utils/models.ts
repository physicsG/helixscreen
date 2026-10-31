// SPDX-License-Identifier: GPL-3.0-or-later
import type { Grouped } from './versions'

const MODEL_ALIASES: Record<string, string> = {
  k1: 'creality k1',
  'zerog mercury hydra 255': 'zero g mercury hydra 255',
  'creality ender 3': 'creality ender 3'
}

// Rows that name no model keep their own entry.
const UNGROUPED = new Set(['Unknown', 'Custom/Other', ''])

/** Lowercase, drop one trailing "(...)", fold separators to single spaces, then apply aliases. */
export function canonicalModelKey(raw: string): string {
  const folded = raw
    .toLowerCase()
    .replace(/\s*\([^)]*\)\s*$/, '')
    .replace(/[\s_-]+/g, ' ')
    .trim()
  return MODEL_ALIASES[folded] ?? folded
}

/** Merges spellings of one printer model; the label is the most common spelling. Ordered by count descending. */
export function canonicalModels(rows: { name: string; count: number }[]): Grouped[] {
  const byKey = new Map<string, { name: string; count: number }[]>()
  for (const row of rows) {
    const key = UNGROUPED.has(row.name) ? `model:${row.name}` : `model:${canonicalModelKey(row.name)}`
    const list = byKey.get(key)
    if (list) list.push(row)
    else byKey.set(key, [row])
  }
  const out: Grouped[] = []
  for (const [key, members] of byKey) {
    let top = members[0]
    for (const m of members) if (m.count > top.count) top = m
    out.push({
      key,
      label: top.name,
      count: members.reduce((sum, m) => sum + m.count, 0),
      members: members.map((m) => m.name),
      expandable: false
    })
  }
  return out.sort((a, b) => b.count - a.count)
}

/**
 * False when the worker's filter parsing would not hand the value back intact
 * (it splits on commas, trims, sanitizes and drops empties), so a click could
 * only return an empty page. The character set is the one in
 * telemetry-worker/src/queries.ts sanitizeFilterValue; a parity test in the
 * worker suite pins the two together.
 */
export function isFilterable(raw: string): boolean {
  return raw !== '' && !raw.includes(',') && raw === raw.trim().replace(/[^a-zA-Z0-9 .\-_()/+]/g, '')
}
