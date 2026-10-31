// SPDX-License-Identifier: GPL-3.0-or-later
import { isFilterable } from './models'
import type { DashFilterKey } from './urlState'
import type { Grouped } from './versions'

/** What a click on a chart row, table cell or card does. */
export type RowAction = 'expand' | 'drill' | 'none'

/**
 * The one drill rule. A version line or beta group toggles open or shut; a
 * row whose every member the worker's filter accepts unchanged becomes a
 * filter; anything else (a sanitized or empty value) does nothing.
 */
export function rowAction(row: Grouped): RowAction {
  if (row.expandable) return 'expand'
  return row.members.length > 0 && row.members.every((m) => m !== '' && isFilterable(m)) ? 'drill' : 'none'
}

const HINT: Record<RowAction, (row: Grouped) => string> = {
  expand: (row) => (row.expanded ? 'click to collapse' : 'click to expand'),
  drill: () => 'click to filter',
  none: () => 'not filterable'
}

/** The tooltip or title line that says what a click on `row` does. */
export function rowHint(row: Grouped | undefined): string {
  return row ? HINT[rowAction(row)](row) : ''
}

/** A chart label: a closed group is marked ▸; an open group's label already ends in ▾. */
export function rowLabel(row: Grouped): string {
  return row.expandable && !row.expanded ? `${row.label} ▸` : row.label
}

/** One raw value as a row, for a table cell or card that filters on it. */
export function single(value: string): Grouped {
  return { key: value, label: value, count: 0, members: [value], expandable: false }
}

export type Activation =
  | { kind: 'expand'; key: string }
  | { kind: 'drill'; filters: Partial<Record<DashFilterKey, string[]>> }
  | null

/**
 * What a click on one or more rows does, one row per filter key (a crash
 * cluster filters version and board at once). A group to expand wins; else
 * every drillable row becomes a filter in one navigation; null when nothing
 * can be done.
 */
export function planActivation(entries: [DashFilterKey, Grouped][]): Activation {
  const toExpand = entries.find(([, row]) => rowAction(row) === 'expand')
  if (toExpand) return { kind: 'expand', key: toExpand[1].key }
  const filters: Partial<Record<DashFilterKey, string[]>> = {}
  for (const [key, row] of entries) if (rowAction(row) === 'drill') filters[key] = row.members
  return Object.keys(filters).length > 0 ? { kind: 'drill', filters } : null
}

/** `planActivation` for a set of rows read as one click: the hint for the whole. */
export function activationHint(entries: [DashFilterKey, Grouped][]): string {
  return planActivation(entries) === null ? 'not filterable' : 'click to filter'
}
