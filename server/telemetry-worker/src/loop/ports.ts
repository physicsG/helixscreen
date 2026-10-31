// SPDX-License-Identifier: GPL-3.0-or-later
// Production adapters for the loop's outside dependencies (GitHub, email, R2). The
// interfaces live in types.ts; loop logic depends on those only.

import { githubFetch, getInstallationToken } from '../../../shared/github'
import { sendEmail } from '../../../shared/email'
import { fingerprintInBody } from './fingerprint'
import { ALL_ISSUE_LABELS, TELEMETRY_LABEL } from './issues'
import type { IssueRef, IssueTracker, Mailer, ObjectStore } from './types'

export type { IssueRef, IssueTracker, Mailer, ObjectStore } from './types'

export interface LoopEnv {
  GITHUB_REPO?: string
  GITHUB_APP_ID?: string
  GITHUB_APP_PRIVATE_KEY?: string
  RESEND_API_KEY?: string
  NOTIFICATION_EMAIL?: string
  EMAIL_FROM?: string
  DASHBOARD_URL?: string
}

const USER_AGENT = 'HelixScreen-Telemetry-Loop'

/** Injection points for tests; production uses the globals and a GitHub App token. */
export interface GithubDeps {
  fetch?: typeof fetch
  getToken?: () => Promise<string>
}

interface GithubIssue {
  number: number
  state: string
  html_url: string
  body: string | null
  labels?: Array<string | { name?: string }>
  /** Present when the item is a pull request; the issues listing returns both. */
  pull_request?: unknown
}

function toRef(issue: GithubIssue): IssueRef {
  return {
    number: issue.number,
    state: issue.state === 'closed' ? 'closed' : 'open',
    url: issue.html_url,
    body: issue.body ?? '',
    labels: (issue.labels ?? []).map((l) => (typeof l === 'string' ? l : (l.name ?? ''))),
  }
}

/** An open issue before a closed one, then the lower number. */
const preferred = (a: IssueRef, b: IssueRef): boolean =>
  a.state !== b.state ? a.state === 'open' : a.number < b.number

/** The path of the rel="next" URL in a Link header, or null on the last page. */
function nextPage(link: string | null): string | null {
  const m = link?.match(/<([^>]+)>\s*;\s*rel="next"/)
  if (!m) return null
  const url = new URL(m[1])
  return `${url.pathname}${url.search}`
}

const LIST_PAGE = 100

/** Resets the memo when the work fails, so a later call tries again. */
function once<T>(work: () => Promise<T>): () => Promise<T> {
  let memo: Promise<T> | null = null
  return () =>
    (memo ??= work().catch((err) => {
      memo = null
      throw err
    }))
}

export function githubIssueTracker(env: LoopEnv, deps: GithubDeps = {}): IssueTracker {
  const fetchImpl = deps.fetch ?? fetch
  let cachedToken: Promise<string> | null = null
  const token = (): Promise<string> => {
    if (deps.getToken) return deps.getToken()
    const { GITHUB_REPO: repo, GITHUB_APP_ID: appId, GITHUB_APP_PRIVATE_KEY: pem } = env
    if (!repo || !appId || !pem) return Promise.reject(new Error('GitHub App is not configured'))
    const [owner, name] = repo.split('/')
    cachedToken ??= getInstallationToken(appId, pem, owner, name)
    return cachedToken
  }
  const repo = env.GITHUB_REPO ?? ''

  const send = async (path: string, options: RequestInit = {}): Promise<Response> =>
    githubFetch(path, await token(), options, USER_AGENT, fetchImpl)
  async function call(path: string, options: RequestInit = {}): Promise<Response> {
    const res = await send(path, options)
    if (!res.ok) throw new Error(`GitHub ${options.method ?? 'GET'} ${path} ${res.status}: ${await res.text()}`)
    return res
  }
  const jsonInit = (method: string, payload: unknown): RequestInit => ({
    method,
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(payload),
  })

  // Every telemetry issue by its marker, from one paged listing per run. Search is not
  // used: it is eventually consistent and its indexing of HTML comments is not relied on.
  const byFingerprint = new Map<string, IssueRef>()
  const add = (ref: IssueRef): void => {
    const fp = fingerprintInBody(ref.body)
    if (fp === null) return
    const known = byFingerprint.get(fp)
    if (!known || preferred(ref, known)) byFingerprint.set(fp, ref)
  }
  const loadIndex = once(async () => {
    let path: string | null = `/repos/${repo}/issues?labels=${TELEMETRY_LABEL}&state=all&per_page=${LIST_PAGE}`
    while (path) {
      const res = await call(path)
      for (const item of (await res.json()) as GithubIssue[]) if (!item.pull_request) add(toRef(item))
      path = nextPage(res.headers.get('Link'))
    }
  })

  // A label missing from the repo can make GitHub reject the create or drop the label.
  const ensureLabels = once(async () => {
    for (const name of ALL_ISSUE_LABELS) {
      const path = `/repos/${repo}/labels`
      const options = jsonInit('POST', { name })
      const res = await send(path, options)
      if (res.ok) continue
      const text = await res.text()
      if (res.status === 422 && text.includes('already_exists')) continue
      throw new Error(`GitHub POST ${path} ${res.status}: ${text}`)
    }
  })

  return {
    repo,
    async findByFingerprint(fp) {
      await loadIndex()
      const ref = byFingerprint.get(fp)
      return ref ? { ...ref } : null
    },
    async create(title, body, labels) {
      await ensureLabels()
      const res = await call(`/repos/${repo}/issues`, jsonInit('POST', { title, body, labels }))
      const ref = toRef((await res.json()) as GithubIssue)
      add(ref)
      return ref
    },
    async comment(issue, body) {
      await call(`/repos/${repo}/issues/${issue}/comments`, jsonInit('POST', { body }))
    },
    async reopen(issue) {
      await call(`/repos/${repo}/issues/${issue}`, jsonInit('PATCH', { state: 'open' }))
    },
  }
}

export function resendMailer(env: LoopEnv, fetchImpl: typeof fetch = fetch): Mailer {
  return {
    async send(message) {
      const { RESEND_API_KEY: apiKey, NOTIFICATION_EMAIL: to, EMAIL_FROM: from } = env
      if (!apiKey || !to || !from) throw new Error('Resend is not configured')
      await sendEmail({ apiKey, from, to }, message, fetchImpl)
    },
  }
}

export function r2Store(bucket: R2Bucket): ObjectStore {
  return {
    async get(key) {
      const obj = await bucket.get(key)
      return obj ? await obj.text() : null
    },
    async put(key, value) {
      await bucket.put(key, value)
    },
    async list(prefix) {
      const keys: string[] = []
      let cursor: string | undefined
      do {
        const page = await bucket.list({ prefix, cursor })
        keys.push(...page.objects.map((o) => o.key))
        cursor = page.truncated ? page.cursor : undefined
      } while (cursor)
      return keys
    },
  }
}
