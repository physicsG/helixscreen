// SPDX-License-Identifier: GPL-3.0-or-later
import { describe, it, expect, vi } from 'vitest'
import { loadState, saveState, alertDue, type LoopState } from '../loop/state'
import { githubIssueTracker, resendMailer, r2Store } from '../loop/ports'
import { markerFor } from '../loop/fingerprint'
import { ALL_ISSUE_LABELS } from '../loop/issues'
import { ALERT_REPEAT_DAYS } from '../loop/thresholds'
import { FakeStore, FakeTracker, FakeMailer } from './loop_fakes'

const dayPlus = (base: string, days: number) =>
  new Date(Date.parse(base) + days * 86_400_000).toISOString().slice(0, 10)

describe('loop state', () => {
  it('round-trips through the store', async () => {
    const store = new FakeStore()
    const state: LoopState = { alerts: { 'a:b': '2026-10-01' }, issueComments: { 'a:b': '2026-W40' } }
    await saveState(store, state)
    expect(store.puts()).toEqual(['loop/state.json'])
    expect(await loadState(store)).toEqual(state)
  })

  it('gives an empty state when nothing is stored', async () => {
    expect(await loadState(new FakeStore())).toEqual({ alerts: {}, issueComments: {} })
  })

  it('gives an empty state, logged, for corrupt JSON', async () => {
    const store = new FakeStore()
    store.data.set('loop/state.json', '{not json')
    const warn = vi.spyOn(console, 'warn').mockImplementation(() => {})
    expect(await loadState(store)).toEqual({ alerts: {}, issueComments: {} })
    expect(warn).toHaveBeenCalledOnce()
    warn.mockRestore()
  })

  it('drops fields of the wrong shape', async () => {
    const store = new FakeStore()
    store.data.set('loop/state.json', JSON.stringify({ alerts: { a: 5, b: '2026-10-01' }, issueComments: 'x' }))
    expect(await loadState(store)).toEqual({ alerts: { b: '2026-10-01' }, issueComments: {} })
  })

  it('alertDue is true for a fingerprint never sent', () => {
    expect(alertDue({ alerts: {}, issueComments: {} }, 'x', '2026-10-15')).toBe(true)
  })

  it('alertDue turns true only after ALERT_REPEAT_DAYS have passed', () => {
    const sent = '2026-09-01'
    const state: LoopState = { alerts: { x: sent }, issueComments: {} }
    expect(alertDue(state, 'x', dayPlus(sent, ALERT_REPEAT_DAYS - 1))).toBe(false)
    expect(alertDue(state, 'x', dayPlus(sent, ALERT_REPEAT_DAYS))).toBe(false)
    expect(alertDue(state, 'x', dayPlus(sent, ALERT_REPEAT_DAYS + 1))).toBe(true)
  })
})

describe('fakes', () => {
  it('the fake tracker finds by fingerprint across open and closed issues', async () => {
    const t = new FakeTracker()
    t.seed(`other\n${markerFor('error_code:e1')}`, 'open')
    const closed = t.seed(`body\n${markerFor('crash_cluster:abc')}`, 'closed')
    const found = await t.findByFingerprint('crash_cluster:abc')
    expect(found?.number).toBe(closed.number)
    expect(found?.state).toBe('closed')
    expect(await t.findByFingerprint('crash_cluster:nope')).toBeNull()
    expect(t.callsOf('find')).toHaveLength(2)
  })

  it('the fake tracker prefers an open issue and records writes', async () => {
    const t = new FakeTracker()
    t.seed(markerFor('x:1'), 'closed')
    const open = t.seed(markerFor('x:1'), 'open')
    expect((await t.findByFingerprint('x:1'))?.number).toBe(open.number)
    const made = await t.create('T', `b ${markerFor('x:2')}`, ['telemetry'])
    await t.comment(made.number, 'hi')
    t.close(made.number)
    await t.reopen(made.number)
    expect(t.callsOf('create')[0].labels).toEqual(['telemetry'])
    expect(t.callsOf('comment')[0]).toMatchObject({ issue: made.number, body: 'hi' })
    expect(t.callsOf('reopen')).toHaveLength(1)
    expect((await t.findByFingerprint('x:2'))?.state).toBe('open')
  })

  it('the fake mailer records messages and records nothing on failure', async () => {
    const m = new FakeMailer()
    await m.send({ subject: 's', html: 'h', text: 't' })
    m.failWith = new Error('down')
    await expect(m.send({ subject: 's2', html: '', text: '' })).rejects.toThrow('down')
    expect(m.sent.map((x) => x.subject)).toEqual(['s'])
  })
})

interface Seen { url: string; init: RequestInit }
interface FakeResponse { status?: number; json?: unknown; text?: string; headers?: Record<string, string> }
function fakeFetch(responses: FakeResponse[]) {
  const seen: Seen[] = []
  const impl = (async (url: string, init: RequestInit = {}) => {
    seen.push({ url: String(url), init })
    const r = responses.shift() ?? {}
    return new Response(r.text ?? JSON.stringify(r.json ?? {}), { status: r.status ?? 200, headers: r.headers })
  }) as unknown as typeof fetch
  return { impl, seen }
}
const env = { GITHUB_REPO: 'acme/widgets' }
const getToken = async () => 'tok'
const issue = (number: number, state: string, body: string, extra: Record<string, unknown> = {}) => ({
  number, state, html_url: `u${number}`, body, labels: [{ name: 'telemetry' }], ...extra,
})
const nextLink = (page: number) => ({
  Link: `<https://api.github.com/repos/acme/widgets/issues?labels=telemetry&state=all&per_page=100&page=${page}>; rel="next", <https://api.github.com/repos/acme/widgets/issues?page=9>; rel="last"`,
})
/** Five label POSTs answered as already existing, so a create can follow. */
const labelsExist = (): FakeResponse[] =>
  ALL_ISSUE_LABELS.map(() => ({ status: 422, json: { errors: [{ code: 'already_exists' }] } }))

describe('githubIssueTracker request shape', () => {
  it('lists telemetry issues in all states and returns the exact marker match', async () => {
    const fp = 'crash_cluster:abc'
    const { impl, seen } = fakeFetch([
      { json: [issue(9, 'open', markerFor('crash_cluster:abcd')), issue(4, 'closed', `x ${markerFor(fp)}`)] },
    ])
    const ref = await githubIssueTracker(env, { fetch: impl, getToken }).findByFingerprint(fp)
    expect(ref).toEqual({ number: 4, state: 'closed', url: 'u4', body: `x ${markerFor(fp)}`, labels: ['telemetry'] })
    const url = new URL(seen[0].url)
    expect(url.pathname).toBe('/repos/acme/widgets/issues')
    expect(url.searchParams.get('labels')).toBe('telemetry')
    expect(url.searchParams.get('state')).toBe('all')
    expect(url.searchParams.get('per_page')).toBe('100')
    expect((seen[0].init.headers as Record<string, string>).Authorization).toBe('Bearer tok')
  })

  it('follows the Link header across pages, once per run', async () => {
    const { impl, seen } = fakeFetch([
      { json: [issue(1, 'open', markerFor('a:1'))], headers: nextLink(2) },
      { json: [issue(2, 'open', markerFor('a:2'))] },
    ])
    const t = githubIssueTracker(env, { fetch: impl, getToken })
    expect((await t.findByFingerprint('a:2'))?.number).toBe(2)
    expect((await t.findByFingerprint('a:1'))?.number).toBe(1)
    expect(seen).toHaveLength(2)
    expect(new URL(seen[1].url).searchParams.get('page')).toBe('2')
  })

  it('prefers an open issue over a closed one, then the lowest number', async () => {
    const fp = 'error_code:x|1'
    const { impl } = fakeFetch([
      { json: [issue(3, 'closed', markerFor(fp)), issue(8, 'open', markerFor(fp))], headers: nextLink(2) },
      { json: [issue(5, 'open', markerFor(fp))] },
    ])
    expect((await githubIssueTracker(env, { fetch: impl, getToken }).findByFingerprint(fp))?.number).toBe(5)
  })

  it('ignores pull requests carrying the marker', async () => {
    const fp = 'a:pr'
    const { impl } = fakeFetch([{ json: [issue(2, 'open', markerFor(fp), { pull_request: { url: 'p' } })] }])
    expect(await githubIssueTracker(env, { fetch: impl, getToken }).findByFingerprint(fp)).toBeNull()
  })

  it('returns null when no issue carries the exact marker', async () => {
    const { impl } = fakeFetch([{ json: [issue(1, 'open', 'nothing')] }])
    expect(await githubIssueTracker(env, { fetch: impl, getToken }).findByFingerprint('a:b')).toBeNull()
  })

  it('finds an issue it created earlier in the same run without listing again', async () => {
    const fp = 'crash_cluster:new'
    const body = markerFor(fp)
    const { impl, seen } = fakeFetch([{ json: [] }, ...labelsExist(), { json: issue(21, 'open', body) }])
    const t = githubIssueTracker(env, { fetch: impl, getToken })
    expect(await t.findByFingerprint(fp)).toBeNull()
    await t.create('T', body, ['telemetry'])
    const calls = seen.length
    expect((await t.findByFingerprint(fp))?.number).toBe(21)
    expect(seen).toHaveLength(calls)
  })

  it('creates an issue with title, body and labels, after ensuring every label once', async () => {
    const { impl, seen } = fakeFetch([
      ...labelsExist(),
      { json: issue(12, 'open', 'B', { labels: [{ name: 'telemetry' }, { name: 'crash' }] }) },
      { json: issue(13, 'open', 'C') },
    ])
    const t = githubIssueTracker(env, { fetch: impl, getToken })
    const ref = await t.create('T', 'B', ['telemetry', 'crash'])
    expect(ref).toMatchObject({ number: 12, state: 'open', url: 'u12', labels: ['telemetry', 'crash'] })
    const labelPosts = seen.slice(0, ALL_ISSUE_LABELS.length)
    expect(labelPosts.every((s) => s.url === 'https://api.github.com/repos/acme/widgets/labels' && s.init.method === 'POST')).toBe(true)
    expect(labelPosts.map((s) => JSON.parse(String(s.init.body)).name)).toEqual([...ALL_ISSUE_LABELS])
    const create = seen[ALL_ISSUE_LABELS.length]
    expect(create.url).toBe('https://api.github.com/repos/acme/widgets/issues')
    expect(create.init.method).toBe('POST')
    expect(JSON.parse(String(create.init.body))).toEqual({ title: 'T', body: 'B', labels: ['telemetry', 'crash'] })
    await t.create('T2', 'C', ['telemetry'])
    expect(seen).toHaveLength(ALL_ISSUE_LABELS.length + 2)
  })

  it('creates the labels that are missing and stops on any other label error', async () => {
    const created = fakeFetch([{ status: 201, json: {} }, ...labelsExist().slice(1), { json: issue(1, 'open', 'B') }])
    await githubIssueTracker(env, { fetch: created.impl, getToken }).create('T', 'B', ['telemetry'])
    expect(created.seen).toHaveLength(ALL_ISSUE_LABELS.length + 1)

    const denied = fakeFetch([{ status: 403, text: 'no permission' }])
    await expect(githubIssueTracker(env, { fetch: denied.impl, getToken }).create('T', 'B', ['telemetry'])).rejects.toThrow(/403.*no permission/)
    expect(denied.seen).toHaveLength(1)

    const invalid = fakeFetch([{ status: 422, json: { errors: [{ code: 'invalid' }] } }])
    await expect(githubIssueTracker(env, { fetch: invalid.impl, getToken }).create('T', 'B', ['telemetry'])).rejects.toThrow(/422/)
  })

  it('comments and reopens', async () => {
    const { impl, seen } = fakeFetch([{}, {}])
    const t = githubIssueTracker(env, { fetch: impl, getToken })
    await t.comment(7, 'again')
    await t.reopen(7)
    expect(seen[0].url).toBe('https://api.github.com/repos/acme/widgets/issues/7/comments')
    expect(seen[0].init.method).toBe('POST')
    expect(JSON.parse(String(seen[0].init.body))).toEqual({ body: 'again' })
    expect(seen[1].url).toBe('https://api.github.com/repos/acme/widgets/issues/7')
    expect(seen[1].init.method).toBe('PATCH')
    expect(JSON.parse(String(seen[1].init.body))).toEqual({ state: 'open' })
  })

  it('throws with the status on a failed call', async () => {
    const { impl } = fakeFetch([{ status: 403, text: 'rate limited' }])
    await expect(githubIssueTracker(env, { fetch: impl, getToken }).reopen(1)).rejects.toThrow(/403.*rate limited/)
  })

  it('rejects when the GitHub App is not configured', async () => {
    const { impl, seen } = fakeFetch([])
    await expect(githubIssueTracker({ GITHUB_REPO: 'a/b' }, { fetch: impl }).reopen(1)).rejects.toThrow(/not configured/)
    expect(seen).toHaveLength(0)
  })
})

describe('resendMailer', () => {
  it('posts to Resend with the configured addresses', async () => {
    const { impl, seen } = fakeFetch([{}])
    await resendMailer({ RESEND_API_KEY: 'k', NOTIFICATION_EMAIL: 'to@x', EMAIL_FROM: 'from@x' }, impl).send({
      subject: 'S', html: '<p>h</p>', text: 'h',
    })
    expect(seen[0].url).toBe('https://api.resend.com/emails')
    expect(JSON.parse(String(seen[0].init.body))).toMatchObject({ from: 'from@x', to: ['to@x'], subject: 'S' })
  })

  it('rejects without calling out when a secret is missing', async () => {
    const { impl, seen } = fakeFetch([])
    await expect(resendMailer({ NOTIFICATION_EMAIL: 'to@x' }, impl).send({ subject: '', html: '', text: '' })).rejects.toThrow(/not configured/)
    expect(seen).toHaveLength(0)
  })
})

describe('r2Store', () => {
  it('reads, writes and lists across pages', async () => {
    const objs = new Map<string, string>()
    const bucket = {
      get: async (k: string) => (objs.has(k) ? { text: async () => objs.get(k)! } : null),
      put: async (k: string, v: string) => void objs.set(k, v),
      list: async ({ prefix, cursor }: { prefix: string; cursor?: string }) => {
        const keys = [...objs.keys()].filter((k) => k.startsWith(prefix)).sort()
        const start = cursor ? Number(cursor) : 0
        const page = keys.slice(start, start + 2)
        const next = start + 2
        return { objects: page.map((key) => ({ key })), truncated: next < keys.length, cursor: String(next) }
      },
    } as unknown as R2Bucket
    const store = r2Store(bucket)
    for (const k of ['w/1', 'w/2', 'w/3', 'z/1']) await store.put(k, k)
    expect(await store.get('w/2')).toBe('w/2')
    expect(await store.get('missing')).toBeNull()
    expect(await store.list('w/')).toEqual(['w/1', 'w/2', 'w/3'])
  })
})
