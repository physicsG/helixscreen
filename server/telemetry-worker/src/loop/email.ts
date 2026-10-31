// SPDX-License-Identifier: GPL-3.0-or-later
// The weekly summary and the alert email. Pure: each email is built as sections of
// rows once, then rendered to HTML and to plain text, so both bodies carry the same
// facts. Fingerprints never appear; findings are named by their titles.

import { escapeHtml, type EmailMessage } from '../../../shared/email'
import type { Finding } from './findings'
import { dashboardLink, type IssueOutcome } from './issues'
import type { WeeklyReport } from './report'

interface Link {
  text: string
  href: string
}
type Cell = string | Link

interface Section {
  title: string
  /** Column names; the first column needs none in the text body. */
  head?: string[]
  rows: Cell[][]
  /** Shown instead of an empty table. */
  empty?: string
}

const cellText = (c: Cell): string => (typeof c === 'string' ? c : `${c.text} ${c.href}`)
const cellHtml = (c: Cell): string =>
  typeof c === 'string' ? escapeHtml(c) : `<a href="${escapeHtml(c.href)}">${escapeHtml(c.text)}</a>`

const TD = 'padding: 4px 12px 4px 0; text-align: left; vertical-align: top;'

function sectionHtml(s: Section): string {
  const parts = [`<h3 style="margin: 20px 0 6px;">${escapeHtml(s.title)}</h3>`]
  if (!s.rows.length) {
    parts.push(`<p style="color: #666;">${escapeHtml(s.empty ?? 'None.')}</p>`)
    return parts.join('\n')
  }
  parts.push('<table style="border-collapse: collapse; font-size: 14px;">')
  if (s.head) parts.push(`<tr>${s.head.map((h) => `<th style="${TD}">${escapeHtml(h)}</th>`).join('')}</tr>`)
  for (const row of s.rows) parts.push(`<tr>${row.map((c) => `<td style="${TD}">${cellHtml(c)}</td>`).join('')}</tr>`)
  parts.push('</table>')
  return parts.join('\n')
}

function sectionText(s: Section): string {
  const lines = [s.title, '-'.repeat(s.title.length)]
  if (!s.rows.length) lines.push(s.empty ?? 'None.')
  for (const row of s.rows) {
    const cells = row.map((c, i) => (i > 0 && s.head?.[i] ? `${s.head[i]} ${cellText(c)}` : cellText(c)))
    lines.push(`- ${cells.join(', ')}`)
  }
  return lines.join('\n')
}

function render(subject: string, intro: string, sections: Section[], footer: Link): EmailMessage {
  const html = [
    '<div style="font-family: sans-serif; max-width: 760px;">',
    `<h2>${escapeHtml(subject)}</h2>`,
    `<p>${escapeHtml(intro)}</p>`,
    ...sections.map(sectionHtml),
    `<p style="margin-top: 20px;">${escapeHtml(footer.text)}: ${cellHtml({ text: footer.href, href: footer.href })}</p>`,
    '</div>',
  ].join('\n')
  const text = [subject, '', intro, '', ...sections.map((s) => `${sectionText(s)}\n`), `${footer.text}: ${footer.href}`].join('\n')
  return { subject, html, text }
}

function signed(v: number, digits: number): string {
  const s = v.toFixed(digits)
  return Number(s) === 0 ? (0).toFixed(digits) : v > 0 ? `+${s}` : s
}

const pct = (v: number | null): string => (v === null ? 'n/a' : `${(v * 100).toFixed(1)}%`)

/** A rate with its change in percentage points, when both windows have one. */
function rateWithChange(cur: number | null, prev: number | null, formatted = pct(cur)): string {
  return cur === null || prev === null ? formatted : `${formatted} (${signed((cur - prev) * 100, 1)} pt)`
}

function countWithChange(cur: number, prev: number): string {
  return prev > 0 ? `${cur} (${signed(((cur - prev) / prev) * 100, 0)}% week over week)` : `${cur}`
}

function fleetSection(report: WeeklyReport): Section {
  const { fleet } = report.health
  const cdn = fleet.cdn_sources === null ? 'not available' : `${fleet.cdn_sources}${fleet.cdn_date ? ` (${fleet.cdn_date})` : ''}`
  return {
    title: 'Fleet',
    rows: [[`Opt-in actives: ${countWithChange(fleet.actives_cur, fleet.actives_prev)}`], [`CDN sources: ${cdn}`]],
  }
}

function releaseSection(report: WeeklyReport): Section {
  return {
    title: 'Releases',
    head: ['Version', 'Devices', 'Crash-free', 'Crash rate', 'Update failure', 'Print success'],
    rows: report.health.releases.map(({ version, cur, prev }) => {
      const attempts = cur.update_failures + cur.update_successes
      const crashRate =
        cur.crash_rate === null
          ? 'n/a'
          : prev.crash_rate === null
            ? `${cur.crash_rate.toFixed(3)} per session`
            : `${cur.crash_rate.toFixed(3)} per session (${signed(cur.crash_rate - prev.crash_rate, 3)})`
      return [
        version,
        countWithChange(cur.devices, prev.devices),
        rateWithChange(cur.crash_free, prev.crash_free),
        crashRate,
        rateWithChange(cur.update_failure_rate, prev.update_failure_rate, `${pct(cur.update_failure_rate)}, ${cur.update_failures} of ${attempts}`),
        rateWithChange(cur.print_success, prev.print_success),
      ]
    }),
    empty: 'No release data.',
  }
}

function errorSection(report: WeeklyReport): Section {
  return {
    title: 'New and rising error codes',
    head: ['Error', 'Status', 'Count', 'Devices'],
    rows: report.health.errors
      .filter((e) => e.status !== 'steady')
      .map((e) => [`${e.category} ${e.code}`, e.status, `${e.cur} (previous ${e.prev})`, `${e.devices}`]),
  }
}

function crashSection(report: WeeklyReport): Section {
  return {
    title: 'Crash clusters',
    head: ['Crash', 'Version', 'Board', 'Crashes', 'Devices'],
    rows: report.health.crash_clusters.map((c) => [
      `${c.signal} ${c.fault_code}`,
      c.version,
      c.board,
      `${c.cur} (previous ${c.prev})`,
      `${c.devices}`,
    ]),
  }
}

function dataHealthSection(report: WeeklyReport): Section {
  return {
    title: 'Data health',
    head: ['Kind', 'Latest day', 'Median', 'Status'],
    rows: report.health.data_health.map((d) => [d.kind, `${d.latest}`, `${d.median}`, d.low ? 'LOW' : 'ok']),
    empty: 'No ingest data.',
  }
}

const ISSUE_ACTIONS: Partial<Record<IssueOutcome['action'], string>> = { created: 'Opened', commented: 'Commented', reopened: 'Reopened' }

function issueSections(report: WeeklyReport): Section[] {
  if (report.issues_not_synced) {
    return [{ title: 'Issues', rows: [[`Issues were not synced: ${report.issues_not_synced}.`]] }]
  }
  const titles = new Map(report.findings.map((f) => [f.fingerprint, f.title]))
  const titleOf = (fp: string): string => titles.get(fp) ?? 'Untitled finding'
  const touched = report.issues.flatMap((o) => {
    const label = ISSUE_ACTIONS[o.action]
    return o.issue && label ? [{ label, title: titleOf(o.fingerprint), url: o.issue.url }] : []
  })
  const held = report.issues.filter((o) => o.action === 'held')
  return [
    {
      title: 'Issues opened or commented on',
      rows: touched.map((t) => [t.label, { text: t.title, href: t.url }]),
    },
    {
      title: 'Held back',
      rows: held.map((o) => [titleOf(o.fingerprint), o.error ? 'GitHub error, retried next week' : 'issue cap, filed next week']),
    },
  ]
}

export function weeklyEmail(report: WeeklyReport, dashboardUrl: string): EmailMessage {
  return render(
    `HelixScreen telemetry, week ${report.week}`,
    `Telemetry for ${report.week}, generated ${report.generated_at}.`,
    [
      fleetSection(report),
      releaseSection(report),
      errorSection(report),
      crashSection(report),
      dataHealthSection(report),
      ...issueSections(report),
    ],
    { text: 'Full report', href: dashboardLink(dashboardUrl, `/reports/${report.week}`) },
  )
}

export function alertEmail(findings: Finding[], dashboardUrl: string, today: string): EmailMessage {
  return render(
    `HelixScreen telemetry alert: ${findings.length} new`,
    `New telemetry alerts on ${today}.`,
    findings.map((f) => ({
      title: f.title,
      rows: [
        ...Object.entries(f.summary).map(([k, v]): Cell[] => [`${k}: ${v}`]),
        [{ text: 'Dashboard', href: dashboardLink(dashboardUrl, f.dashboardPath) }],
      ],
    })),
    { text: 'Dashboard', href: dashboardLink(dashboardUrl, '/') },
  )
}
