// SPDX-License-Identifier: GPL-3.0-or-later
// Every number the feedback loop decides on. Nothing else in the loop hard-codes one.

/** Distinct devices a crash cluster needs before it raises an alert. */
export const CLUSTER_ALERT_DEVICES = 3
/** Distinct devices a crash cluster needs before it files an issue. */
export const CLUSTER_ISSUE_DEVICES = 3
/** Distinct devices an error code needs before it files an issue. */
export const ERROR_ISSUE_DEVICES = 5
/** Distinct devices failing the same update step before an issue is filed. */
export const UPDATE_FAILURE_ISSUE_DEVICES = 3
/** Devices a release needs before its crash-free rate is compared to the previous one. */
export const RELEASE_REGRESSION_MIN_DEVICES = 20
/** Crash-free rate drop (as a fraction) that marks a release as a regression. */
export const RELEASE_REGRESSION_CRASH_FREE_DROP = 0.02
/** Devices reporting an unknown printer before a printer-DB candidate issue is filed. */
export const PRINTER_ISSUE_DEVICES = 5
/** New issues a single run may file; the rest wait for the next run. */
export const NEW_ISSUES_PER_RUN = 5
/** Days before the same alert may be emailed again. */
export const ALERT_REPEAT_DAYS = 30
