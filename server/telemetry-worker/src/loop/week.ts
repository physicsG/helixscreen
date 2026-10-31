// SPDX-License-Identifier: GPL-3.0-or-later
// ISO 8601 week keys (Monday start, week 1 holds Jan 4), all in UTC.

const DAY_MS = 86_400_000

export function isoWeek(date: Date): string {
  // The Thursday of a date's week decides which ISO year the week belongs to.
  const day = date.getUTCDay() || 7
  const thursday = Date.UTC(date.getUTCFullYear(), date.getUTCMonth(), date.getUTCDate() + 4 - day)
  const year = new Date(thursday).getUTCFullYear()
  const week = Math.floor((thursday - Date.UTC(year, 0, 1)) / DAY_MS / 7) + 1
  return `${year}-W${String(week).padStart(2, '0')}`
}

export function previousIsoWeek(date: Date): string {
  return isoWeek(new Date(date.getTime() - 7 * DAY_MS))
}

export function isMonday(date: Date): boolean {
  return date.getUTCDay() === 1
}
