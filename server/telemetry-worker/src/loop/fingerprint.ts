// SPDX-License-Identifier: GPL-3.0-or-later
// Stable identity for a finding, embedded in its GitHub issue body so a later run
// finds the same issue again instead of opening a second one.

export type FindingKind =
  | 'crash_cluster'
  | 'error_code'
  | 'update_failure'
  | 'release_regression'
  | 'data_health'
  | 'printer'

const MARKER_RE = /<!--\s*telemetry-fp:\s*(.+?)\s*-->/

/** Longest a fingerprint part may be, after sanitizing. */
export const FINGERPRINT_PART_MAX = 64

/**
 * One part in the only form a fingerprint carries: ASCII whitespace runs collapsed and
 * trimmed, ASCII lowercased, every code point outside [a-z0-9._:/+-] replaced by "_",
 * cut to FINGERPRINT_PART_MAX. Parts come from devices, so this keeps "|", "-->" and
 * newlines out of the marker. scripts/telemetry-printer-issues.py implements the same
 * rule; tests/fixtures/telemetry_issue_vectors.json binds the two.
 */
export function fingerprintPart(part: string): string {
  return part
    .replace(/[ \t\n\r\f\v]+/g, ' ')
    .replace(/^ | $/g, '')
    .replace(/[A-Z]/g, (c) => c.toLowerCase())
    .replace(/[^a-z0-9._:/+-]/gu, '_')
    .slice(0, FINGERPRINT_PART_MAX)
}

/** "<kind>:" followed by the sanitized parts joined with "|". */
export function fingerprint(kind: FindingKind, parts: string[]): string {
  return `${kind}:${parts.map(fingerprintPart).join('|')}`
}

export function markerFor(fp: string): string {
  return `<!-- telemetry-fp: ${fp} -->`
}

/** The fingerprint stored in an issue body by markerFor, or null when there is none. */
export function fingerprintInBody(body: string): string | null {
  const m = MARKER_RE.exec(body)
  return m ? m[1] : null
}
