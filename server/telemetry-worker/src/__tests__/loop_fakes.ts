// SPDX-License-Identifier: GPL-3.0-or-later
// In-memory IssueTracker, Mailer and ObjectStore for the loop tests. Each records
// every call so a test can assert what was (or was not) sent, filed or written.

import type { IssueRef, IssueTracker, Mailer, ObjectStore } from '../loop/ports'
import type { EmailMessage } from '../../../shared/email'
import { fingerprintInBody } from '../loop/fingerprint'

export type TrackerCall =
  | { op: 'find'; fp: string }
  | { op: 'create'; title: string; body: string; labels: string[] }
  | { op: 'comment'; issue: number; body: string }
  | { op: 'reopen'; issue: number }

export class FakeTracker implements IssueTracker {
  readonly repo = 'acme/widgets'
  issues: (IssueRef & { title: string; labels: string[]; comments: string[] })[] = []
  calls: TrackerCall[] = []
  /** When set, every call rejects with it. */
  failWith: Error | null = null
  /** When it returns an error for a call, that call rejects with it after being recorded. */
  failWhen: ((call: TrackerCall) => Error | null) | null = null
  private nextNumber = 1

  private check(): void {
    if (this.failWith) throw this.failWith
    const err = this.failWhen?.(this.calls[this.calls.length - 1])
    if (err) throw err
  }

  /** Seeds an existing issue, as if filed by an earlier run. */
  seed(body: string, state: 'open' | 'closed' = 'open', title = 'seeded'): IssueRef {
    const issue = {
      number: this.nextNumber++,
      state,
      url: `https://github.test/issues/${this.nextNumber - 1}`,
      body,
      title,
      labels: ['telemetry'],
      comments: [] as string[],
    }
    this.issues.push(issue)
    return issue
  }

  close(issue: number): void {
    this.issues.find((i) => i.number === issue)!.state = 'closed'
  }

  async findByFingerprint(fp: string): Promise<IssueRef | null> {
    this.calls.push({ op: 'find', fp })
    this.check()
    const hits = this.issues.filter((i) => i.labels.includes('telemetry') && fingerprintInBody(i.body) === fp)
    hits.sort((a, b) => Number(b.state === 'open') - Number(a.state === 'open') || a.number - b.number)
    return hits[0] ? { ...hits[0] } : null
  }

  async create(title: string, body: string, labels: string[]): Promise<IssueRef> {
    this.calls.push({ op: 'create', title, body, labels })
    this.check()
    const made = this.seed(body, 'open', title)
    made.labels = labels
    return { ...made }
  }

  async comment(issue: number, body: string): Promise<void> {
    this.calls.push({ op: 'comment', issue, body })
    this.check()
    this.issues.find((i) => i.number === issue)!.comments.push(body)
  }

  async reopen(issue: number): Promise<void> {
    this.calls.push({ op: 'reopen', issue })
    this.check()
    this.issues.find((i) => i.number === issue)!.state = 'open'
  }

  callsOf<T extends TrackerCall['op']>(op: T): Extract<TrackerCall, { op: T }>[] {
    return this.calls.filter((c): c is Extract<TrackerCall, { op: T }> => c.op === op)
  }
}

export class FakeMailer implements Mailer {
  /** Every send, including the ones that failed. */
  attempts: EmailMessage[] = []
  sent: EmailMessage[] = []
  /** When set, send rejects with it and nothing is recorded as sent. */
  failWith: Error | null = null

  async send(message: EmailMessage): Promise<void> {
    this.attempts.push(message)
    if (this.failWith) throw this.failWith
    this.sent.push(message)
  }
}

export type StoreCall = { op: 'get' | 'put' | 'list'; key: string }

export class FakeStore implements ObjectStore {
  data = new Map<string, string>()
  calls: StoreCall[] = []
  /** When it returns true for a key, get rejects for that key after being recorded. */
  failGet: (key: string) => boolean = () => false
  /** When it returns true for a key, put rejects for that key and stores nothing. */
  failPut: (key: string) => boolean = () => false

  async get(key: string): Promise<string | null> {
    this.calls.push({ op: 'get', key })
    if (this.failGet(key)) throw new Error(`injected get failure: ${key}`)
    return this.data.get(key) ?? null
  }

  async put(key: string, value: string): Promise<void> {
    this.calls.push({ op: 'put', key })
    if (this.failPut(key)) throw new Error(`injected put failure: ${key}`)
    this.data.set(key, value)
  }

  async list(prefix: string): Promise<string[]> {
    this.calls.push({ op: 'list', key: prefix })
    return [...this.data.keys()].filter((k) => k.startsWith(prefix)).sort()
  }

  puts(): string[] {
    return this.calls.filter((c) => c.op === 'put').map((c) => c.key)
  }
}
