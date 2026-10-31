// SPDX-License-Identifier: GPL-3.0-or-later
// Builds the loop's dependencies from the Worker environment and runs it. Reads come
// from Analytics Engine, state and reports live in R2.

import { fetchHealth } from "../health_source";
import { executeQuery, updateFailureGroupsQuery, HEALTH_WINDOWS, type QueryConfig } from "../queries";
import { num, rowsOf, str } from "../schema";
import type { UpdateFailureGroup } from "./findings";
import { githubIssueTracker, r2Store, resendMailer, type LoopEnv } from "./ports";
import { runLoop, type LoopDeps } from "./run";
import { isMonday } from "./week";

export interface LoopWorkerEnv extends LoopEnv {
  TELEMETRY_BUCKET: R2Bucket;
  CLOUDFLARE_ACCOUNT_ID: string;
  HELIX_ANALYTICS_READ_TOKEN?: string;
}

/** Null when the Analytics Engine credentials are missing. */
export function queryConfigOf(env: LoopWorkerEnv): QueryConfig | null {
  if (!env.CLOUDFLARE_ACCOUNT_ID || !env.HELIX_ANALYTICS_READ_TOKEN) return null;
  return { accountId: env.CLOUDFLARE_ACCOUNT_ID, apiToken: env.HELIX_ANALYTICS_READ_TOKEN };
}

export function loopDeps(env: LoopWorkerEnv, config: QueryConfig, now: Date): LoopDeps {
  return {
    health: () => fetchHealth(config, now.toISOString().slice(0, 10)),
    async updateGroups(): Promise<UpdateFailureGroup[]> {
      const res = await executeQuery(config, updateFailureGroupsQuery(HEALTH_WINDOWS.cur));
      return rowsOf(res).map((r) => ({
        platform: str(r.platform),
        reason: str(r.reason),
        failures: num(r.failures),
        devices: num(r.devices),
      }));
    },
    tracker: env.GITHUB_REPO && env.GITHUB_APP_ID && env.GITHUB_APP_PRIVATE_KEY ? githubIssueTracker(env) : null,
    mailer: env.RESEND_API_KEY ? resendMailer(env) : null,
    store: r2Store(env.TELEMETRY_BUCKET),
    now,
    dashboardUrl: env.DASHBOARD_URL ?? "",
  };
}

/**
 * The 13:00 UTC cron: the daily run, plus the weekly run on Mondays. Each run is
 * isolated, so a failure is logged and neither stops the other nor rejects.
 */
export async function runScheduledLoop(env: LoopWorkerEnv, now: Date): Promise<void> {
  const config = queryConfigOf(env);
  if (!config) {
    console.error(`[loop] ${now.toISOString()} skipped: Analytics Engine not configured`);
    return;
  }
  const modes: Array<"daily" | "weekly"> = isMonday(now) ? ["daily", "weekly"] : ["daily"];
  for (const mode of modes) {
    try {
      const result = await runLoop(mode, loopDeps(env, config, now), false);
      const line = `[loop] ${new Date().toISOString()} ${mode} run: alertsSent=${JSON.stringify(result.alertsSent)} skipped=${JSON.stringify(result.skipped)}`;
      if (result.skipped.length) console.error(line);
      else console.log(line);
    } catch (err) {
      console.error(`[loop] ${new Date().toISOString()} ${mode} run failed: ${err instanceof Error ? err.message : String(err)}`);
    }
  }
}
