// SPDX-License-Identifier: GPL-3.0-or-later
import { describe, it, expect } from 'vitest'
import { alertEmail, weeklyEmail } from '../loop/email'
import type { Finding } from '../loop/findings'
import type { WeeklyReport } from '../loop/report'
import type { HealthPayload } from '../health'

const URL = 'https://dash.test/'
const WEEK = '2026-W42'
const HEX64 = /[0-9a-f]{64}/i
const EVIL = '<script>alert(1)</script>'

function finding(title: string, over: Partial<Finding> = {}): Finding {
  return {
    kind: 'crash_cluster',
    fingerprint: `crash_cluster:${'ab'.repeat(32)}|${title}`,
    actions: ['alert', 'issue', 'report'],
    title,
    summary: { signal: 'SIGSEGV', devices: 4, crashes: 9 },
    dashboardPath: '/stability?version=1.0.4',
    ...over,
  }
}

function health(): HealthPayload {
  return {
    fleet: { actives_cur: 220, actives_prev: 200, cdn_sources: 950, cdn_date: '2026-10-11' },
    releases: [
      {
        version: '1.0.4',
        cur: { devices: 120, update_failures: 3, update_successes: 97, crash_free: 0.95, crash_rate: 0.012, update_failure_rate: 0.03, print_success: 0.9 },
        prev: { devices: 100, update_failures: 1, update_successes: 99, crash_free: 0.97, crash_rate: 0.01, update_failure_rate: 0.01, print_success: 0.92 },
      },
    ],
    errors: [
      { category: 'moonraker', code: EVIL, cur: 30, prev: 0, devices: 7, status: 'new' },
      { category: 'klippy', code: 'steady_code', cur: 5, prev: 5, devices: 2, status: 'steady' },
    ],
    crash_clusters: [{ signal: 'SIGSEGV', fault_code: 'SEGV_MAPERR', version: '1.0.4', board: 'rpi4', cur: 9, prev: 0, devices: 4 }],
    data_health: [{ kind: 'session', latest: 10, median: 100, low: true }],
    not_applicable: { crashes: null, errors: null, prints: null, updates: null },
  }
}

function report(): WeeklyReport {
  const created = finding(`Crash ${EVIL}`)
  const commented = finding('Crash commented', { fingerprint: 'crash_cluster:b' })
  const capped = finding('Crash held by cap', { fingerprint: 'crash_cluster:c' })
  const failed = finding('Crash held by error', { fingerprint: 'crash_cluster:d' })
  return {
    week: WEEK,
    generated_at: '2026-10-19T13:00:00.000Z',
    health: health(),
    findings: [created, commented, capped, failed],
    issues: [
      { fingerprint: created.fingerprint, action: 'created', issue: { number: 11, url: 'https://github.test/issues/11' } },
      { fingerprint: commented.fingerprint, action: 'commented', issue: { number: 12, url: 'https://github.test/issues/12' } },
      { fingerprint: capped.fingerprint, action: 'held' },
      { fingerprint: failed.fingerprint, action: 'held', error: 'GitHub POST 502' },
    ],
  }
}

describe('weeklyEmail', () => {
  const mail = weeklyEmail(report(), URL)

  it('has the week in the subject and links the stored report', () => {
    expect(mail.subject).toBe(`HelixScreen telemetry, week ${WEEK}`)
    expect(mail.html).toContain(`https://dash.test/reports/${WEEK}`)
    expect(mail.text).toContain(`https://dash.test/reports/${WEEK}`)
  })

  it('escapes interpolated values in the HTML', () => {
    expect(mail.html).not.toContain('<script>')
    expect(mail.html).toContain('&lt;script&gt;')
  })

  it('carries the same facts in the text body', () => {
    for (const body of [mail.text, mail.html]) {
      expect(body).toContain('220')
      expect(body).toContain('950')
      expect(body).toContain('1.0.4')
      expect(body).toContain('95.0%')
      expect(body).toContain('-2.0 pt')
      expect(body).toContain('3 of 100')
      expect(body).toContain('SEGV_MAPERR')
      expect(body).toContain('session')
      expect(body).toContain('https://github.test/issues/11')
      expect(body).toContain('https://github.test/issues/12')
      expect(body).toContain('Crash held by cap')
      expect(body).toContain('Crash held by error')
    }
    expect(mail.text).toContain(`Crash ${EVIL}`)
  })

  it('lists only new and rising error codes', () => {
    expect(mail.text).not.toContain('steady_code')
    expect(mail.html).not.toContain('steady_code')
  })

  it('carries no 64-hex string', () => {
    expect(mail.html).not.toMatch(HEX64)
    expect(mail.text).not.toMatch(HEX64)
  })
})

describe('weeklyEmail without issue sync', () => {
  const mail = weeklyEmail({ ...report(), issues: [], issues_not_synced: 'GitHub is not configured' }, URL)

  it('says issues were not synced and why, and promises no retry', () => {
    for (const body of [mail.text, mail.html]) {
      expect(body).toContain('Issues were not synced: GitHub is not configured.')
      expect(body).not.toContain('retried next week')
      expect(body).not.toContain('Issues opened or commented on')
    }
  })
})

describe('alertEmail', () => {
  const findings = [finding(`Crash ${EVIL}`), finding('Ingest of session is low', { kind: 'data_health', dashboardPath: '/' })]
  const mail = alertEmail(findings, URL, '2026-10-19')

  it('counts the findings in the subject', () => {
    expect(mail.subject).toBe('HelixScreen telemetry alert: 2 new')
  })

  it('lists each finding with its counts and dashboard link in both bodies', () => {
    for (const body of [mail.text, mail.html]) {
      expect(body).toContain('Ingest of session is low')
      expect(body).toContain('https://dash.test/stability?version=1.0.4')
      expect(body).toContain('SIGSEGV')
      expect(body).toContain('2026-10-19')
    }
    expect(mail.text).toContain(`Crash ${EVIL}`)
  })

  it('escapes interpolated values in the HTML', () => {
    expect(mail.html).not.toContain('<script>')
    expect(mail.html).toContain('Crash &lt;script&gt;alert(1)&lt;/script&gt;')
  })

  it('carries no 64-hex string', () => {
    expect(mail.html).not.toMatch(HEX64)
    expect(mail.text).not.toMatch(HEX64)
  })
})
