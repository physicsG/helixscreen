// SPDX-License-Identifier: GPL-3.0-or-later
// One run of the feedback loop. Daily: email the alert findings not sent recently.
// Weekly: snapshot the week that just closed, sync its issues and email the summary.
// A dry run reads everything and returns what it would send, but sends, files and
// writes nothing.

import type { EmailMessage } from '../../../shared/email'
import type { HealthPayload } from '../health'
import { alertEmail, weeklyEmail } from './email'
import { findingsFromHealth, type Finding, type UpdateFailureGroup } from './findings'
import { syncIssues, type IssueOutcome } from './issues'
import type { IssueTracker, Mailer, ObjectStore } from './types'
import { reportKey, type WeeklyReport } from './report'
import { alertDue, loadState, saveState, type LoopState } from './state'
import { isoWeek, previousIsoWeek } from './week'

export interface LoopDeps {
  health(): Promise<HealthPayload>
  updateGroups(): Promise<UpdateFailureGroup[]>
  /** Null when the GitHub App is not configured. */
  tracker: IssueTracker | null
  /** Null when email is not configured. */
  mailer: Mailer | null
  store: ObjectStore
  now: Date
  dashboardUrl: string
}

export interface LoopResult {
  mode: 'daily' | 'weekly'
  week?: string
  findings: Finding[]
  /** Fingerprints whose alert was emailed and recorded by this run. */
  alertsSent: string[]
  issues: IssueOutcome[]
  /** The email this run built. It went out unless the run was dry or reused, or `skipped` names the email step. */
  email?: EmailMessage
  /** True when the week's report was already stored and was returned unchanged. */
  reused?: boolean
  /** Steps that did not happen, with the reason. */
  skipped: string[]
}

const NOT_CONFIGURED = 'email: not configured'
const GITHUB_NOT_CONFIGURED = 'github: not configured'
const GITHUB_REASON = 'GitHub is not configured'

const message = (err: unknown): string => (err instanceof Error ? err.message : String(err))

function warn(what: string, err: unknown): void {
  console.warn(`[loop] ${new Date().toISOString()} ${what}: ${message(err)}`)
}

/** Sends the email, or says in `skipped` why it was not sent. True only when it was sent. */
async function deliver(mailer: Mailer | null, email: EmailMessage, skipped: string[]): Promise<boolean> {
  if (!mailer) {
    skipped.push(NOT_CONFIGURED)
    return false
  }
  try {
    await mailer.send(email)
    return true
  } catch (err) {
    warn('email failed', err)
    skipped.push(`email: ${message(err)}`)
    return false
  }
}

/** Saves the state, or logs the failure and lists it in `skipped`. True only when saved. */
async function persist(store: ObjectStore, state: LoopState, skipped: string[]): Promise<boolean> {
  try {
    await saveState(store, state)
    return true
  } catch (err) {
    warn('state save failed', err)
    skipped.push(`state: ${message(err)}`)
    return false
  }
}

async function runDaily(deps: LoopDeps, dryRun: boolean): Promise<LoopResult> {
  const today = deps.now.toISOString().slice(0, 10)
  const state = await loadState(deps.store)
  const findings = findingsFromHealth(await deps.health(), await deps.updateGroups(), isoWeek(deps.now))
  const result: LoopResult = { mode: 'daily', findings, alertsSent: [], issues: [], skipped: [] }

  const due = findings.filter((f) => f.actions.includes('alert') && alertDue(state, f.fingerprint, today))
  if (!due.length) return result
  result.email = alertEmail(due, deps.dashboardUrl, today)
  if (dryRun) return result

  // An alert is recorded only once its email went out, so a failed send retries tomorrow.
  if (!(await deliver(deps.mailer, result.email, result.skipped))) return result
  for (const f of due) state.alerts[f.fingerprint] = today
  if (await persist(deps.store, state, result.skipped)) result.alertsSent = due.map((f) => f.fingerprint)
  return result
}

async function runWeekly(deps: LoopDeps, dryRun: boolean): Promise<LoopResult> {
  const week = previousIsoWeek(deps.now)
  const key = reportKey(week)

  // A stored report is never rewritten; a re-run returns it as stored.
  const stored = await deps.store.get(key)
  if (stored !== null) {
    const report = JSON.parse(stored) as WeeklyReport
    return {
      mode: 'weekly',
      week,
      findings: report.findings,
      alertsSent: [],
      issues: report.issues,
      email: weeklyEmail(report, deps.dashboardUrl),
      reused: true,
      skipped: [],
    }
  }

  const state = await loadState(deps.store)
  const health = await deps.health()
  const findings = findingsFromHealth(health, await deps.updateGroups(), week)
  const report: WeeklyReport = { week, generated_at: deps.now.toISOString(), health, findings, issues: [] }
  const skipped: string[] = []
  if (deps.tracker) {
    report.issues = await syncIssues(findings, deps.tracker, state, week, deps.dashboardUrl, dryRun)
    const titles = new Map(findings.map((f) => [f.fingerprint, f.title]))
    for (const o of report.issues) if (o.error) skipped.push(`github: ${titles.get(o.fingerprint)}: ${o.error}`)
  } else {
    report.issues_not_synced = GITHUB_REASON
    skipped.push(GITHUB_NOT_CONFIGURED)
  }
  const { issues } = report

  const email = weeklyEmail(report, deps.dashboardUrl)
  const result: LoopResult = { mode: 'weekly', week, findings, alertsSent: [], issues, email, skipped }
  if (dryRun) return result

  try {
    await deps.store.put(key, JSON.stringify(report))
  } catch (err) {
    // Issues were already filed or commented on; keep that record so a re-run does not comment twice.
    await persist(deps.store, state, skipped)
    throw err
  }
  await deliver(deps.mailer, email, skipped)
  await persist(deps.store, state, skipped)
  return result
}

export async function runLoop(mode: 'daily' | 'weekly', deps: LoopDeps, dryRun: boolean): Promise<LoopResult> {
  return mode === 'daily' ? runDaily(deps, dryRun) : runWeekly(deps, dryRun)
}
