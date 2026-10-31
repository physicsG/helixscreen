// SPDX-License-Identifier: GPL-3.0-or-later
import { compareVersions } from './chart'

/** Shared row shape for every grouped distribution. */
export interface Grouped {
  /** Stable id: the raw value, or 'line:0.99', 'betas:1.1.0', 'model:<canonical key>', 'set:PETG + PLA'. */
  key: string
  /** What the chart shows. */
  label: string
  /** Summed over members. */
  count: number
  /** Raw values to send as a filter, in input order. */
  members: string[]
  /** A version line or beta group: a click toggles it between one row and its members. */
  expandable: boolean
  /** An expandable group shown open: this row is its header and its members follow it. */
  expanded?: boolean
}

interface Parsed {
  raw: string
  count: number
  core: string
  line: string
  prerelease: boolean
}

function parse(row: { name: string; count: number }): Parsed {
  const noBuild = row.name.replace(/^v/, '').split('+')[0]
  const dash = noBuild.indexOf('-')
  const core = dash < 0 ? noBuild : noBuild.slice(0, dash)
  const line = core.split('.').slice(0, 2).join('.')
  return { raw: row.name, count: row.count, core, line, prerelease: dash >= 0 }
}

function groupOf(key: string, label: string, parts: Parsed[], expandable: boolean, expanded = false): Grouped {
  const group: Grouped = {
    key,
    label,
    count: parts.reduce((sum, p) => sum + p.count, 0),
    members: parts.map((p) => p.raw),
    expandable
  }
  if (expanded) group.expanded = true
  return group
}

/**
 * Collapses a long version list into something readable. The `keepLines`
 * newest major.minor lines are shown version by version; older lines fold to
 * one "<x.y>.x (<n> versions)" row. In a kept line, prereleases whose release
 * is not in `rows` fold to one "<x.y.z> betas (<n>)" row. A group whose key is
 * in `expanded` renders as a header row ("<x.y>.x ▾", "<x.y.z> betas ▾",
 * `expanded: true`, every member) followed by its members, so a click on the
 * header can collapse it again. Output is newest first; `n` counts members,
 * not devices.
 */
export function groupVersions(
  rows: { name: string; count: number }[],
  expanded: ReadonlySet<string>,
  keepLines = 2
): Grouped[] {
  const parsed = rows.map(parse).sort((a, b) => compareVersions(b.raw, a.raw))
  const lines: string[] = []
  for (const p of parsed) if (!lines.includes(p.line)) lines.push(p.line)
  const kept = new Set(lines.slice(0, keepLines))
  const releases = new Set(parsed.filter((p) => !p.prerelease).map((p) => p.core))

  const out: Grouped[] = []
  const emitted = new Set<string>()
  const individual = (p: Parsed) =>
    out.push({ key: p.raw, label: p.raw, count: p.count, members: [p.raw], expandable: false })

  const inputOrder = (members: Parsed[]) =>
    members.slice().sort((a, b) => rows.findIndex((r) => r.name === a.raw) - rows.findIndex((r) => r.name === b.raw))

  for (const p of parsed) {
    if (kept.has(p.line)) {
      if (p.prerelease && !releases.has(p.core)) {
        const key = `betas:${p.core}`
        if (!emitted.has(key)) {
          emitted.add(key)
          const members = inputOrder(parsed.filter((q) => q.prerelease && q.core === p.core))
          const open = expanded.has(key)
          out.push(groupOf(key, open ? `${p.core} betas ▾` : `${p.core} betas (${members.length})`, members, true, open))
        }
        if (expanded.has(key)) individual(p)
      } else {
        individual(p)
      }
    } else {
      const key = `line:${p.line}`
      if (!emitted.has(key)) {
        emitted.add(key)
        const members = inputOrder(parsed.filter((q) => q.line === p.line))
        const open = expanded.has(key)
        out.push(groupOf(key, open ? `${p.line}.x ▾` : `${p.line}.x (${members.length} versions)`, members, true, open))
      }
      if (expanded.has(key)) individual(p)
    }
  }
  return out
}

/** One row per raw value, for a dimension that needs no grouping (platforms, boards). */
export function ungrouped(rows: { name: string; count: number }[]): Grouped[] {
  return rows.map((r) => ({ key: r.name, label: r.name, count: r.count, members: [r.name], expandable: false }))
}

/**
 * The version rows of `rows` on lines older than the `keepLines` newest lines
 * of `reference`, grouped as groupVersions does. `reference` is the version
 * list that decides which lines are current: pass the unfiltered list so a
 * version filter cannot make an old line look current.
 */
export function olderLines(
  rows: { name: string; count: number }[],
  reference: { name: string; count: number }[],
  expanded: ReadonlySet<string>,
  keepLines = 2
): Grouped[] {
  const current = new Set<string>()
  for (const p of reference.map(parse).sort((a, b) => compareVersions(b.raw, a.raw))) {
    if (current.size >= keepLines) break
    current.add(p.line)
  }
  return groupVersions(rows, expanded, keepLines).filter(
    (g) => !g.members.every((m) => current.has(parse({ name: m, count: 0 }).line))
  )
}

/**
 * Version rows for a rate chart: versions grouped as groupVersions does, each
 * row's rate recombined from its members' numerators and denominators
 * (crashes over sessions, successes over prints), never an average of rates.
 * A row whose members all have a null rate (the filters make the rate
 * inapplicable) keeps a null rate; so does a row with a zero denominator.
 */
export function groupVersionRates(
  rows: { version: string; numerator: number; denominator: number; rate: number | null }[],
  expanded: ReadonlySet<string>,
  keepLines = 2
): (Grouped & { numerator: number; denominator: number; rate: number | null })[] {
  const byVersion = new Map(rows.map((r) => [r.version, r]))
  return groupVersions(
    rows.map((r) => ({ name: r.version, count: r.denominator })),
    expanded,
    keepLines
  ).map((g) => {
    const members = g.members.map((m) => byVersion.get(m)!)
    const numerator = members.reduce((sum, m) => sum + m.numerator, 0)
    const denominator = members.reduce((sum, m) => sum + m.denominator, 0)
    const applies = members.some((m) => m.rate !== null)
    return { ...g, numerator, denominator, rate: applies && denominator > 0 ? numerator / denominator : null }
  })
}

/**
 * Version-picker chips: versions grouped as groupVersions does, each with how
 * many of its members are in `shown`. A closed group whose `shown` is above 0
 * holds a picked version the picker does not list on its own.
 */
export function versionChips(
  rows: { name: string; count: number }[],
  expanded: ReadonlySet<string>,
  shown: ReadonlySet<string>,
  keepLines = 2
): (Grouped & { shown: number })[] {
  return groupVersions(rows, expanded, keepLines).map((g) => ({
    ...g,
    shown: g.members.filter((m) => shown.has(m)).length
  }))
}
