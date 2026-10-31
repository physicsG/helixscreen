import type { ActiveElement, Chart, ChartEvent, ChartOptions } from 'chart.js'
import { formatRate } from './format'
import { rowHint } from './drill'
import type { Grouped } from './versions'

export const horizontalBarOpts: ChartOptions<'bar'> = {
  indexAxis: 'y',
  scales: { y: { ticks: { autoSkip: false } } }
}

/** Axis label color on every chart. */
export const TICK_COLOR = '#94a3b8'

const AXIS_STYLE = {
  ticks: { color: TICK_COLOR },
  grid: { color: 'rgba(45, 51, 72, 0.5)' }
}

/** An axis whose values are 0-1 ratios, labelled as percentages. */
export function rateAxis(digits = 0) {
  return {
    ...AXIS_STYLE,
    min: 0,
    ticks: { ...AXIS_STYLE.ticks, callback: (value: string | number) => formatRate(Number(value), digits) }
  }
}

function isPlainObject(v: unknown): v is Record<string, unknown> {
  return typeof v === 'object' && v !== null && Object.getPrototypeOf(v) === Object.prototype
}

/**
 * Overlays `over` on `base`: plain objects merge key by key, anything else
 * (arrays, functions, numbers) from `over` replaces what `base` had. A chart
 * that sets one tooltip callback keeps the theme's tooltip colors.
 */
export function mergeOptions<T>(base: T, over: unknown): T {
  if (!isPlainObject(base) || !isPlainObject(over)) return (over === undefined ? base : over) as T
  const out: Record<string, unknown> = { ...base }
  for (const [key, value] of Object.entries(over)) out[key] = mergeOptions(out[key], value)
  return out as T
}

/** A chart.js onClick that reports the element under the pointer; a click on empty space reports nothing. */
export function selectOnClick(report: (index: number, label: string) => void) {
  return (evt: ChartEvent, _elements: ActiveElement[], chart: Chart) => {
    if (!evt.native) return
    const hit = chart.getElementsAtEventForMode(evt.native, 'nearest', { intersect: true }, true)[0]
    if (hit) report(hit.index, String(chart.data.labels?.[hit.index] ?? ''))
  }
}

// Identical semantics to telemetry-worker/src/versions.ts; change both together.
function parsePrerelease(pre: string): Array<number | string> {
  return pre.split('.').map((id) => (/^\d+$/.test(id) ? Number(id) : id))
}

/**
 * Orders '1.2.3' and '1.2.3-beta.4' style versions (leading 'v' ignored,
 * '+build' metadata ignored). Numeric parts compare numerically, a release
 * sorts after any prerelease of the same version, and prerelease identifiers
 * compare numerically when both are numeric (beta.9 < beta.10), otherwise as
 * text, with numeric identifiers below text ones.
 */
export function compareVersions(a: string, b: string): number {
  const split = (v: string): [number[], string | null] => {
    const noBuild = v.replace(/^v/, '').split('+')[0]
    const dash = noBuild.indexOf('-')
    const core = dash < 0 ? noBuild : noBuild.slice(0, dash)
    const pre = dash < 0 ? null : noBuild.slice(dash + 1)
    return [core.split('.').map((n) => Number(n) || 0), pre]
  }
  const [coreA, preA] = split(a)
  const [coreB, preB] = split(b)
  for (let i = 0; i < Math.max(coreA.length, coreB.length); i++) {
    const diff = (coreA[i] ?? 0) - (coreB[i] ?? 0)
    if (diff !== 0) return diff
  }
  if (preA === null && preB === null) return 0
  if (preA === null) return 1
  if (preB === null) return -1
  const ia = parsePrerelease(preA)
  const ib = parsePrerelease(preB)
  for (let i = 0; i < Math.min(ia.length, ib.length); i++) {
    const x = ia[i]
    const y = ib[i]
    if (x === y) continue
    if (typeof x === 'number' && typeof y === 'number') return x - y
    if (typeof x === 'number') return -1
    if (typeof y === 'number') return 1
    return x < y ? -1 : 1
  }
  return ia.length - ib.length
}

/** One bar of a success-rate chart: `count` outcomes, `members` the raw values it filters on. */
export type SuccessRow = Grouped & { rate: number | null }

/**
 * Options for a horizontal bar chart of 0-1 success rates whose tooltip gives
 * the sample size and, when the bars are clickable, what a click does.
 */
export function successBarOpts(rows: SuccessRow[], clickable = true): ChartOptions<'bar'> {
  return {
    indexAxis: 'y',
    scales: { x: { ...rateAxis(), max: 1 }, y: { ticks: { autoSkip: false } } },
    plugins: {
      legend: { display: false },
      tooltip: {
        callbacks: {
          label: (ctx) => {
            const row = rows[ctx.dataIndex]
            return row ? `${formatRate(row.rate)} of ${row.count.toLocaleString()} prints` : ''
          },
          afterLabel: (ctx) => (clickable ? rowHint(rows[ctx.dataIndex]) : '')
        }
      }
    }
  }
}
