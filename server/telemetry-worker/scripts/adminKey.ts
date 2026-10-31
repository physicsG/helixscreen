// SPDX-License-Identifier: GPL-3.0-or-later
// Admin key lookup shared by the operator scripts.

import { existsSync, readFileSync } from "node:fs";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";

/**
 * Value of NAME in KEY=VALUE text, or undefined. Blank lines, # comments, an
 * optional `export ` prefix and one layer of matching quotes are understood;
 * nothing is evaluated. The last assignment wins, as in a sourced file.
 */
export function parseEnvValue(text: string, name: string): string | undefined {
  let value: string | undefined;
  for (const raw of text.split(/\r?\n/)) {
    const m = raw.match(/^\s*(?:export\s+)?([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(.*?)\s*$/);
    if (!m || m[1] !== name) continue;
    let v = m[2];
    if (v.length >= 2 && (v[0] === '"' || v[0] === "'") && v.endsWith(v[0])) v = v.slice(1, -1);
    value = v;
  }
  return value;
}

/** The admin key from HELIX_TELEMETRY_ADMIN_KEY, else the repo root's .env.telemetry. */
export function adminKeyFromEnvOrFile(): string | undefined {
  const fromEnv = process.env.HELIX_TELEMETRY_ADMIN_KEY;
  if (fromEnv) return fromEnv;
  const envFile = resolve(dirname(fileURLToPath(import.meta.url)), "../../../.env.telemetry");
  if (!existsSync(envFile)) return undefined;
  return parseEnvValue(readFileSync(envFile, "utf8"), "HELIX_TELEMETRY_ADMIN_KEY") || undefined;
}
