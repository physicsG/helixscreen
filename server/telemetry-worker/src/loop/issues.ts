// SPDX-License-Identifier: GPL-3.0-or-later
// Files, updates and reopens one GitHub issue per issue-worthy finding, keyed by the
// fingerprint marker in the issue body.

import type { Finding } from './findings'
import { markerFor, type FindingKind } from './fingerprint'
import type { IssueTracker } from './types'
import type { LoopState } from './state'
import { NEW_ISSUES_PER_RUN } from './thresholds'

export interface IssueOutcome {
  fingerprint: string
  action: 'created' | 'commented' | 'reopened' | 'unchanged' | 'held'
  issue?: { number: number; url: string }
  /**
   * What went wrong: why a finding was held when GitHub failed (absent when the per-run
   * cap held it), or what is wrong with an issue that was created.
   */
  error?: string
}

/** Every issue a producer files carries it; lookups list by it. */
export const TELEMETRY_LABEL = 'telemetry'

const LABELS: Partial<Record<FindingKind, string>> = {
  crash_cluster: 'crash',
  error_code: 'error-code',
  update_failure: 'update-failure',
  printer: 'printer-db',
}

/** Every label a telemetry issue can carry, telemetry first. */
export const ALL_ISSUE_LABELS: readonly string[] = [TELEMETRY_LABEL, ...Object.values(LABELS)]

export function issueLabels(kind: FindingKind): string[] {
  const label = LABELS[kind]
  return label ? [TELEMETRY_LABEL, label] : [TELEMETRY_LABEL]
}

/** An issue without the label is invisible to the next run's lookup, which would file it again. */
const UNLABELLED = `created without the ${TELEMETRY_LABEL} label; add it by hand or the next run files a duplicate`

/** Longest device-supplied value a table cell shows, in code points. */
const CELL_MAX = 100
/** Longest issue title, in code points; GitHub rejects titles over 256 characters. */
const TITLE_MAX = 200

/** Cuts text to `max` code points, marking the cut with "...". */
function cut(text: string, max: number): string {
  const points = [...text]
  return points.length > max ? `${points.slice(0, max).join('')}...` : text
}

/** One line, cut to `max`; a zero-width space after "@" keeps it from starting a mention. */
const safeLine = (text: string, max: number): string => cut(text.replace(/\r\n|\r|\n/g, ' '), max).replace(/@/g, '@\u200b')

/**
 * Device-supplied text as a code span safe in a Markdown table cell: one line, no "|"
 * to split the row, no backtick to end the span, no mention. The Python producer applies
 * the same rule; tests/fixtures/telemetry_issue_vectors.json binds the two.
 */
export function codeSpan(text: string): string {
  if (!text) return ''
  return `\`${safeLine(text, CELL_MAX).replace(/`/g, "'").replace(/\|/g, '\\|')}\``
}

/** An issue title built from device-supplied text: one line, no mention, within GitHub's limit. */
export function issueTitle(title: string): string {
  return safeLine(title, TITLE_MAX)
}

const cell = (v: string | number): string => (typeof v === 'number' ? String(v) : codeSpan(v))

function countsTable(f: Finding): string {
  const rows = Object.entries(f.summary).map(([k, v]) => `| ${k} | ${cell(v)} |`)
  return ['| Field | Value |', '| --- | --- |', ...rows].join('\n')
}

/** A dashboard route under the configured base URL, whether or not the base ends in "/". */
export function dashboardLink(dashboardUrl: string, path: string): string {
  return `${dashboardUrl.replace(/\/+$/, '')}${path}`
}

/**
 * For a crash cluster, the start of the crash worker's fingerprint
 * (`<signal>/<version>/<frame>`) that its crash-report issues carry, with a search for
 * them, so a matching report can be linked by hand. Telemetry has no frame.
 */
function crashReportLines(f: Finding, repo: string): string[] {
  if (f.kind !== 'crash_cluster') return []
  const prefix = `${f.summary.signal}/${f.summary.version}/`
  const q = `is:issue label:crash "Fingerprint: ${prefix}" in:body`
  return [
    `Crash reports for this cluster carry a fingerprint starting ${codeSpan(prefix)}: <https://github.com/${repo}/issues?q=${encodeURIComponent(q)}>`,
    '',
  ]
}

/** Marker first, then the counts, any crash-report link, the dashboard link and the ISO week the counts cover. */
export function issueBody(f: Finding, dashboardUrl: string, week: string, repo: string): string {
  return [
    markerFor(f.fingerprint),
    '',
    `Seen in telemetry for ${week}.`,
    '',
    countsTable(f),
    '',
    ...crashReportLines(f, repo),
    `Dashboard: ${dashboardLink(dashboardUrl, f.dashboardPath)}`,
  ].join('\n')
}

function commentBody(f: Finding, dashboardUrl: string, week: string, returned: boolean): string {
  const lead = returned ? `This returned in ${week}.` : `Still seen in ${week}.`
  return [lead, '', countsTable(f), '', `Dashboard: ${dashboardLink(dashboardUrl, f.dashboardPath)}`].join('\n')
}

/**
 * Applies every issue-bearing finding to the tracker. Mutates `state.issueComments`
 * (the caller saves it) unless `dryRun`, which reports the same outcomes and calls
 * nothing that writes. A tracker error holds that finding and moves on.
 */
export async function syncIssues(
  findings: Finding[],
  tracker: IssueTracker,
  state: LoopState,
  week: string,
  dashboardUrl: string,
  dryRun: boolean,
): Promise<IssueOutcome[]> {
  const outcomes: IssueOutcome[] = []
  let created = 0
  const remember = (fp: string): void => {
    if (!dryRun) state.issueComments[fp] = week
  }

  const handled = new Set<string>()
  for (const f of findings) {
    if (!f.actions.includes('issue') || handled.has(f.fingerprint)) continue
    const fp = f.fingerprint
    handled.add(fp)
    try {
      const existing = await tracker.findByFingerprint(fp)
      if (existing) {
        const ref = { number: existing.number, url: existing.url }
        if (existing.state === 'closed') {
          if (!dryRun) {
            await tracker.reopen(existing.number)
            await tracker.comment(existing.number, commentBody(f, dashboardUrl, week, true))
          }
          remember(fp)
          outcomes.push({ fingerprint: fp, action: 'reopened', issue: ref })
        } else if (state.issueComments[fp] === week) {
          outcomes.push({ fingerprint: fp, action: 'unchanged', issue: ref })
        } else {
          if (!dryRun) await tracker.comment(existing.number, commentBody(f, dashboardUrl, week, false))
          remember(fp)
          outcomes.push({ fingerprint: fp, action: 'commented', issue: ref })
        }
      } else if (created >= NEW_ISSUES_PER_RUN) {
        outcomes.push({ fingerprint: fp, action: 'held' })
      } else {
        created++
        if (dryRun) {
          outcomes.push({ fingerprint: fp, action: 'created' })
        } else {
          const made = await tracker.create(issueTitle(f.title), issueBody(f, dashboardUrl, week, tracker.repo), issueLabels(f.kind))
          remember(fp)
          const outcome: IssueOutcome = { fingerprint: fp, action: 'created', issue: { number: made.number, url: made.url } }
          if (!made.labels.includes(TELEMETRY_LABEL)) {
            console.error(`[loop] ${new Date().toISOString()} issue #${made.number} ${UNLABELLED}`)
            outcome.error = UNLABELLED
          }
          outcomes.push(outcome)
        }
      }
    } catch (err) {
      const error = err instanceof Error ? err.message : String(err)
      console.warn(`[loop] ${new Date().toISOString()} issue sync failed for ${fp}: ${error}`)
      outcomes.push({ fingerprint: fp, action: 'held', error })
    }
  }
  return outcomes
}
