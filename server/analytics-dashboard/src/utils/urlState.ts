// SPDX-License-Identifier: GPL-3.0-or-later

export type DashFilterKey = 'platform' | 'version' | 'model' | 'board'

export interface DashQuery {
  range: string
  filters: Record<DashFilterKey, string[]>
  expand: string[]
}

export const DASH_FILTER_KEYS: DashFilterKey[] = ['platform', 'version', 'model', 'board']

const DEFAULT_RANGE = '30d'

const LEGACY_PATHS: Record<string, string> = {
  '/adoption': '/fleet',
  '/hardware': '/fleet',
  '/memory': '/stability',
  '/crashes': '/stability',
  '/reliability': '/stability',
  '/engagement': '/usage',
  '/features': '/usage',
  '/ux': '/usage'
}

type RawValue = string | null | undefined | (string | null)[]
type RawQuery = Record<string, RawValue>

function first(v: RawValue): string {
  const s = Array.isArray(v) ? v[0] : v
  return typeof s === 'string' ? s : ''
}

function splitList(v: RawValue): string[] {
  return first(v)
    .split(',')
    .map((s) => s.trim())
    .filter((s) => s.length > 0)
}

export function parseDashQuery(q: RawQuery): DashQuery {
  const range = first(q.range)
  return {
    range: /^\d+d$/.test(range) ? range : DEFAULT_RANGE,
    filters: {
      platform: splitList(q.platform),
      version: splitList(q.version),
      model: splitList(q.model),
      board: splitList(q.board)
    },
    expand: splitList(q.expand)
  }
}

export function buildDashQuery(s: DashQuery): Record<string, string> {
  const out: Record<string, string> = { range: s.range }
  for (const key of DASH_FILTER_KEYS) {
    if (s.filters[key].length) out[key] = s.filters[key].join(',')
  }
  if (s.expand.length) out.expand = s.expand.join(',')
  return out
}

export function applyDrill(s: DashQuery, key: DashFilterKey, members: string[]): DashQuery {
  return applyDrills(s, { [key]: members })
}

/** Sets several filters in one step, so one navigation (one history entry) carries them all. */
export function applyDrills(s: DashQuery, drills: Partial<Record<DashFilterKey, string[]>>): DashQuery {
  const filters = { ...s.filters }
  for (const key of DASH_FILTER_KEYS) {
    const members = drills[key]
    if (members) filters[key] = [...new Set(members)]
  }
  return { range: s.range, filters, expand: [...s.expand] }
}

/** A worker query string with one filter removed, for data that must ignore it. */
export function withoutFilter(queryString: string, key: DashFilterKey): string {
  const params = new URLSearchParams(queryString)
  params.delete(key)
  return params.toString()
}

export function legacyRedirect(path: string): string | null {
  return LEGACY_PATHS[path] ?? null
}

/** Every path that redirects to a tab. */
export const LEGACY_ROUTES: string[] = Object.keys(LEGACY_PATHS)

/** Where an old bookmark lands: its tab, with the query (filters, range) carried over unchanged. */
export function legacyLocation<Q>(path: string, query: Q): { path: string; query: Q } | null {
  const target = legacyRedirect(path)
  return target === null ? null : { path: target, query }
}

export function sameDashQuery(a: DashQuery, b: DashQuery): boolean {
  const qa = buildDashQuery(a)
  const qb = buildDashQuery(b)
  const keys = Object.keys(qa)
  return keys.length === Object.keys(qb).length && keys.every((k) => qa[k] === qb[k])
}
