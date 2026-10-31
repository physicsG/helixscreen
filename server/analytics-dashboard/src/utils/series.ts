// SPDX-License-Identifier: GPL-3.0-or-later

const SERIES_PALETTE = [
  '#3b82f6',
  '#10b981',
  '#f59e0b',
  '#ef4444',
  '#8b5cf6',
  '#ec4899',
  '#06b6d4',
  '#84cc16',
  '#f97316',
  '#14b8a6',
  '#a855f7',
  '#eab308'
]

/** Today as YYYY-MM-DD in UTC, the timezone daily buckets use. */
export function todayUtc(): string {
  return new Date().toISOString().slice(0, 10)
}

/** Drops the in-progress day and the earliest day (a partial bucket at the window edge). One or two days leave nothing. */
export function trimPartialDays<T extends { date: string }>(series: T[], today: string): T[] {
  if (series.length <= 2) return []
  const earliest = series.reduce((min, p) => (p.date < min ? p.date : min), series[0].date)
  return series.filter((p) => p.date !== today && p.date !== earliest)
}

export function seriesColor(index: number): string {
  return SERIES_PALETTE[index % SERIES_PALETTE.length]
}

/** A '#rrggbb' color at 35% opacity, for a row drawn but played down. */
export function dimmed(color: string): string {
  return `${color}59`
}

/** Pixel height for a horizontal bar chart so every row keeps a readable label. */
export function barChartHeight(rows: number): number {
  return Math.max(300, rows * 22 + 60)
}
