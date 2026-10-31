// SPDX-License-Identifier: GPL-3.0-or-later
import type { Grouped } from './versions'

/** Normalizes a ';'-separated filament list to its distinct materials, e.g. 'PLA;PLA;PETG' to 'PETG + PLA'. */
export function materialSet(filamentType: string): { set: string; materials: string[]; multi: boolean } {
  const materials = [
    ...new Set(
      filamentType
        .split(';')
        .map((m) => m.trim().toUpperCase())
        .filter(Boolean)
    )
  ].sort()
  return { set: materials.join(' + '), materials, multi: materials.length >= 2 }
}

/** A material set with fewer prints than this is drawn dimmed: its rate rests on too few prints to compare. */
export const LOW_COUNT_PRINTS = 5

export function isLowCount(count: number): boolean {
  return count < LOW_COUNT_PRINTS
}

/**
 * Sums rows that share a material set; success_rate is the count-weighted
 * mean, or null for a set with no prints. Ordered by count descending.
 */
export function groupMaterialSets(
  rows: { type: string; count: number; success_rate: number }[]
): (Grouped & { success_rate: number | null; multi: boolean })[] {
  const bySet = new Map<string, { multi: boolean; rows: typeof rows }>()
  for (const row of rows) {
    const { set, multi } = materialSet(row.type)
    const entry = bySet.get(set)
    if (entry) entry.rows.push(row)
    else bySet.set(set, { multi, rows: [row] })
  }
  const out: (Grouped & { success_rate: number | null; multi: boolean })[] = []
  for (const [set, { multi, rows: members }] of bySet) {
    const count = members.reduce((sum, m) => sum + m.count, 0)
    const weighted = members.reduce((sum, m) => sum + m.count * m.success_rate, 0)
    out.push({
      key: `set:${set}`,
      label: set,
      count,
      members: members.map((m) => m.type),
      expandable: false,
      success_rate: count > 0 ? weighted / count : null,
      multi
    })
  }
  return out.sort((a, b) => b.count - a.count)
}
