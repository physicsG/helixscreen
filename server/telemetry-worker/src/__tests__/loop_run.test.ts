// SPDX-License-Identifier: GPL-3.0-or-later
import { describe, it, expect } from 'vitest'
import { runLoop, type LoopDeps } from '../loop/run'
import { loopDeps } from '../loop/wiring'
import { reportKey, type WeeklyReport } from '../loop/report'
import { loadState } from '../loop/state'
import { previousIsoWeek } from '../loop/week'
import type { HealthPayload } from '../health'
import type { UpdateFailureGroup } from '../loop/findings'
import { CLUSTER_ALERT_DEVICES } from '../loop/thresholds'
import { FakeMailer, FakeStore, FakeTracker } from './loop_fakes'

const MONDAY = new Date('2026-10-19T13:00:00Z')
const TODAY = '2026-10-19'
const WEEK = previousIsoWeek(MONDAY)
const URL = 'https://dash.test'
const STATE_KEY = 'loop/state.json'

const cluster = (faultCode: string, devices = CLUSTER_ALERT_DEVICES) => ({
  signal: 'SIGSEGV',
  fault_code: faultCode,
  version: '1.0.4',
  board: 'rpi4',
  cur: devices * 2,
  prev: 0,
  devices,
})

/** Two crash clusters, each an alert and an issue. */
function health(): HealthPayload {
  return {
    fleet: { actives_cur: 220, actives_prev: 200, cdn_sources: null, cdn_date: null },
    releases: [],
    errors: [],
    crash_clusters: [cluster('SEGV_MAPERR'), cluster('SEGV_ACCERR')],
    data_health: [],
    not_applicable: { crashes: null, errors: null, prints: null, updates: null },
  }
}

interface Rig {
  deps: LoopDeps
  tracker: FakeTracker
  mailer: FakeMailer
  store: FakeStore
}

function rig(over: Partial<LoopDeps> = {}): Rig {
  const tracker = new FakeTracker()
  const mailer = new FakeMailer()
  const store = new FakeStore()
  const deps: LoopDeps = {
    health: async () => health(),
    updateGroups: async (): Promise<UpdateFailureGroup[]> => [],
    tracker,
    mailer,
    store,
    now: MONDAY,
    dashboardUrl: URL,
    ...over,
  }
  return { deps, tracker, mailer, store }
}

const storedReport = (store: FakeStore): WeeklyReport => JSON.parse(store.data.get(reportKey(WEEK))!)

describe('runLoop daily', () => {
  it('sends one email for two due alerts and records both', async () => {
    const r = rig()
    const out = await runLoop('daily', r.deps, false)
    expect(out.mode).toBe('daily')
    expect(r.mailer.sent).toHaveLength(1)
    expect(r.mailer.sent[0].subject).toBe('HelixScreen telemetry alert: 2 new')
    expect(out.alertsSent).toHaveLength(2)
    const state = await loadState(r.store)
    expect(Object.keys(state.alerts).sort()).toEqual([...out.alertsSent].sort())
    expect(Object.values(state.alerts)).toEqual([TODAY, TODAY])
    expect(r.tracker.calls).toEqual([])
  })

  it('sends nothing on a second run the same day', async () => {
    const r = rig()
    await runLoop('daily', r.deps, false)
    const again = await runLoop('daily', r.deps, false)
    expect(again.alertsSent).toEqual([])
    expect(again.email).toBeUndefined()
    expect(r.mailer.attempts).toHaveLength(1)
  })

  it('leaves both alerts unrecorded when the mailer throws, so the next day retries', async () => {
    const r = rig()
    r.mailer.failWith = new Error('Resend API 500')
    const out = await runLoop('daily', r.deps, false)
    expect(r.mailer.attempts).toHaveLength(1)
    expect(out.alertsSent).toEqual([])
    expect(out.skipped.join('\n')).toContain('Resend API 500')
    expect((await loadState(r.store)).alerts).toEqual({})

    r.mailer.failWith = null
    const retry = await runLoop('daily', { ...r.deps, now: new Date('2026-10-20T13:00:00Z') }, false)
    expect(retry.alertsSent).toHaveLength(2)
    expect(r.mailer.sent).toHaveLength(1)
  })

  it('records nothing and says so when no mailer is configured', async () => {
    const r = rig({ mailer: null })
    const out = await runLoop('daily', r.deps, false)
    expect(out.skipped).toContain('email: not configured')
    expect(out.alertsSent).toEqual([])
    expect(r.store.data.has(STATE_KEY)).toBe(false)
  })

  it('keeps the result and lists the failure when saving the state fails after the send', async () => {
    const r = rig()
    r.store.failPut = (key) => key === STATE_KEY
    const out = await runLoop('daily', r.deps, false)
    expect(r.mailer.sent).toHaveLength(1)
    expect(out.alertsSent).toEqual([])
    expect(out.skipped.join('\n')).toContain(`state: injected put failure: ${STATE_KEY}`)
  })

  it('sends nothing when reading the state fails', async () => {
    const r = rig()
    r.store.failGet = (key) => key === STATE_KEY
    await expect(runLoop('daily', r.deps, false)).rejects.toThrow('injected get failure')
    expect(r.mailer.attempts).toEqual([])
  })

  it('in a dry run returns the email and writes nothing', async () => {
    const r = rig()
    const out = await runLoop('daily', r.deps, true)
    expect(out.email?.subject).toBe('HelixScreen telemetry alert: 2 new')
    expect(out.alertsSent).toEqual([])
    expect(r.mailer.attempts).toEqual([])
    expect(r.store.puts()).toEqual([])
  })
})

describe('runLoop weekly', () => {
  it('stores the report for the previous week, files issues and emails the summary', async () => {
    const r = rig()
    const out = await runLoop('weekly', r.deps, false)
    expect(out.week).toBe(WEEK)
    expect(out.reused).toBeUndefined()
    expect(r.tracker.callsOf('create')).toHaveLength(2)
    expect(out.issues.map((i) => i.action)).toEqual(['created', 'created'])

    const report = storedReport(r.store)
    expect(report.week).toBe(WEEK)
    expect(report.generated_at).toBe(MONDAY.toISOString())
    expect(report.findings).toEqual(out.findings)
    expect(report.issues).toEqual(out.issues)

    expect(r.mailer.sent.map((m) => m.subject)).toEqual([`HelixScreen telemetry, week ${WEEK}`])
    const state = await loadState(r.store)
    expect(Object.keys(state.issueComments)).toHaveLength(2)
    expect(state.alerts).toEqual({})
    expect(out.alertsSent).toEqual([])
  })

  it('stores the report before emailing', async () => {
    const r = rig()
    let storedAtSend = false
    r.deps.mailer = { send: async () => void (storedAtSend = r.store.data.has(reportKey(WEEK))) }
    await runLoop('weekly', r.deps, false)
    expect(storedAtSend).toBe(true)
  })

  it('returns the stored report on a re-run and files, sends and writes nothing', async () => {
    const r = rig()
    const first = await runLoop('weekly', r.deps, false)
    const stored = r.store.data.get(reportKey(WEEK))
    const callsBefore = r.tracker.calls.length
    const putsBefore = r.store.puts().length

    let healthCalls = 0
    const again = await runLoop('weekly', { ...r.deps, health: async () => (healthCalls++, health()) }, false)
    expect(again.reused).toBe(true)
    expect(again.week).toBe(WEEK)
    expect(again.issues).toEqual(first.issues)
    expect(again.findings).toEqual(first.findings)
    expect(healthCalls).toBe(0)
    expect(r.tracker.calls.length).toBe(callsBefore)
    expect(r.store.puts().length).toBe(putsBefore)
    expect(r.store.data.get(reportKey(WEEK))).toBe(stored)
    expect(r.mailer.attempts).toHaveLength(1)
  })

  it('holds a finding whose create fails and still emails the rest', async () => {
    const r = rig()
    r.tracker.failWhen = (call) => (call.op === 'create' && call.title.includes('SEGV_ACCERR') ? new Error('GitHub POST 502') : null)
    const out = await runLoop('weekly', r.deps, false)
    const held = out.issues.filter((i) => i.action === 'held')
    expect(held).toHaveLength(1)
    expect(held[0].error).toContain('GitHub POST 502')
    expect(out.issues.filter((i) => i.action === 'created')).toHaveLength(1)
    expect(out.skipped.some((s) => s.includes('GitHub POST 502'))).toBe(true)
    expect(r.mailer.sent).toHaveLength(1)
    expect(r.mailer.sent[0].text).toContain('SEGV_ACCERR')
    expect(storedReport(r.store).issues).toEqual(out.issues)
  })

  it('lists a created issue that lacks the telemetry label in skipped', async () => {
    const r = rig()
    const orig = r.tracker.create.bind(r.tracker)
    r.tracker.create = async (title, body) => ({ ...(await orig(title, body, [])), labels: [] })
    const out = await runLoop('weekly', r.deps, false)
    expect(out.issues.map((i) => i.action)).toEqual(['created', 'created'])
    expect(out.skipped.filter((s) => s.includes('telemetry label'))).toHaveLength(2)
  })

  it('keeps the stored report and state when the mailer throws', async () => {
    const r = rig()
    r.mailer.failWith = new Error('Resend API 500')
    const out = await runLoop('weekly', r.deps, false)
    expect(out.skipped.join('\n')).toContain('Resend API 500')
    expect(r.store.data.has(reportKey(WEEK))).toBe(true)
    expect(Object.keys((await loadState(r.store)).issueComments)).toHaveLength(2)
  })

  it('emails nothing when the report cannot be stored, and keeps the issue state', async () => {
    const r = rig()
    r.store.failPut = (key) => key === reportKey(WEEK)
    await expect(runLoop('weekly', r.deps, false)).rejects.toThrow('injected put failure')
    expect(r.mailer.attempts).toEqual([])
    expect(Object.keys((await loadState(r.store)).issueComments)).toHaveLength(2)
  })

  it('keeps the stored report, the email and the result when saving the state fails', async () => {
    const r = rig()
    r.store.failPut = (key) => key === STATE_KEY
    const out = await runLoop('weekly', r.deps, false)
    expect(r.store.data.has(reportKey(WEEK))).toBe(true)
    expect(r.mailer.sent).toHaveLength(1)
    expect(out.issues.map((i) => i.action)).toEqual(['created', 'created'])
    expect(out.skipped.join('\n')).toContain(`state: injected put failure: ${STATE_KEY}`)
  })

  it('skips issue sync with one line when GitHub is not configured, and the email says so', async () => {
    const r = rig({ tracker: null })
    const out = await runLoop('weekly', r.deps, false)
    expect(out.skipped).toEqual(['github: not configured'])
    expect(out.issues).toEqual([])
    expect(storedReport(r.store).issues_not_synced).toBe('GitHub is not configured')
    expect(r.mailer.sent[0].text).toContain('Issues were not synced: GitHub is not configured.')
    expect(r.mailer.sent[0].text).not.toContain('retried next week')
  })

  it('says the same in a dry run without GitHub', async () => {
    const out = await runLoop('weekly', rig({ tracker: null }).deps, true)
    expect(out.skipped).toEqual(['github: not configured'])
    expect(out.email?.text).toContain('Issues were not synced')
  })

  it('says so when no mailer is configured', async () => {
    const r = rig({ mailer: null })
    const out = await runLoop('weekly', r.deps, false)
    expect(out.skipped).toContain('email: not configured')
    expect(r.store.data.has(reportKey(WEEK))).toBe(true)
  })

  it('in a dry run returns the email and would-be issues, and files, sends and writes nothing', async () => {
    const r = rig()
    const out = await runLoop('weekly', r.deps, true)
    expect(out.email?.subject).toBe(`HelixScreen telemetry, week ${WEEK}`)
    expect(out.issues.map((i) => i.action)).toEqual(['created', 'created'])
    expect(r.tracker.calls.every((c) => c.op === 'find')).toBe(true)
    expect(r.mailer.attempts).toEqual([])
    expect(r.store.puts()).toEqual([])
  })
})

describe('loopDeps', () => {
  const config = { accountId: 'a', apiToken: 't' }
  const base = { TELEMETRY_BUCKET: {} as R2Bucket, CLOUDFLARE_ACCOUNT_ID: 'a' }
  const github = { GITHUB_REPO: 'acme/widgets', GITHUB_APP_ID: '1', GITHUB_APP_PRIVATE_KEY: 'pem' }

  it('has no tracker when any GitHub App setting is missing', () => {
    for (const missing of Object.keys(github)) {
      const env = { ...base, ...github, [missing]: '' }
      expect(loopDeps(env, config, MONDAY).tracker).toBeNull()
    }
  })

  it('has a tracker for the configured repo when all are set', () => {
    expect(loopDeps({ ...base, ...github }, config, MONDAY).tracker?.repo).toBe('acme/widgets')
  })
})
