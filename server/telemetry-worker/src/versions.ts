// SPDX-License-Identifier: GPL-3.0-or-later
// Version ordering for release strings. The dashboard keeps an identical copy
// in analytics-dashboard/src/utils/chart.ts; change both together.

function parsePrerelease(pre: string): Array<number | string> {
  return pre.split(".").map((id) => (/^\d+$/.test(id) ? Number(id) : id));
}

/**
 * Orders "1.2.3" and "1.2.3-beta.4" style versions (leading "v" ignored,
 * "+build" metadata ignored). Numeric parts compare numerically, a release
 * sorts after any prerelease of the same version, and prerelease identifiers
 * compare numerically when both are numeric (beta.9 < beta.10), otherwise as
 * text, with numeric identifiers below text ones.
 */
export function compareVersions(a: string, b: string): number {
  const split = (v: string): [number[], string | null] => {
    const noBuild = v.replace(/^v/, "").split("+")[0];
    const dash = noBuild.indexOf("-");
    const core = dash < 0 ? noBuild : noBuild.slice(0, dash);
    const pre = dash < 0 ? null : noBuild.slice(dash + 1);
    return [core.split(".").map((n) => Number(n) || 0), pre];
  };
  const [coreA, preA] = split(a);
  const [coreB, preB] = split(b);
  for (let i = 0; i < Math.max(coreA.length, coreB.length); i++) {
    const diff = (coreA[i] ?? 0) - (coreB[i] ?? 0);
    if (diff !== 0) return diff;
  }
  if (preA === null && preB === null) return 0;
  if (preA === null) return 1;
  if (preB === null) return -1;
  const ia = parsePrerelease(preA);
  const ib = parsePrerelease(preB);
  for (let i = 0; i < Math.min(ia.length, ib.length); i++) {
    const x = ia[i];
    const y = ib[i];
    if (x === y) continue;
    if (typeof x === "number" && typeof y === "number") return x - y;
    if (typeof x === "number") return -1;
    if (typeof y === "number") return 1;
    return x < y ? -1 : 1;
  }
  return ia.length - ib.length;
}
