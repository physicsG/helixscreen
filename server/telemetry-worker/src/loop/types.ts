// SPDX-License-Identifier: GPL-3.0-or-later
// The loop's three outside dependencies (GitHub, email, R2) as small interfaces. No
// Worker runtime types, so modules the dashboard imports can depend on them; the
// production adapters live in ports.ts.

import type { EmailMessage } from '../../../shared/email'

export interface IssueRef {
  number: number
  state: 'open' | 'closed'
  url: string
  body: string
  labels: string[]
}

export interface IssueTracker {
  /** owner/name of the repository the issues live in. */
  readonly repo: string
  /**
   * The issue whose body carries the fingerprint's marker, among open and closed issues
   * labelled telemetry: an open one first, then the lowest number.
   */
  findByFingerprint(fp: string): Promise<IssueRef | null>
  create(title: string, body: string, labels: string[]): Promise<IssueRef>
  comment(issue: number, body: string): Promise<void>
  reopen(issue: number): Promise<void>
}

export interface Mailer {
  send(message: EmailMessage): Promise<void>
}

export interface ObjectStore {
  get(key: string): Promise<string | null>
  put(key: string, value: string): Promise<void>
  list(prefix: string): Promise<string[]>
}
