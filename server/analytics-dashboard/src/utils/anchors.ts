// SPDX-License-Identifier: GPL-3.0-or-later

/** The element id of an error code's row on the Stability tab; links from Health target it. */
export function errorAnchor(code: string): string {
  return `error-${code.replace(/[^A-Za-z0-9_-]/g, '_')}`
}
