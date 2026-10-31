// SPDX-License-Identifier: GPL-3.0-or-later
// Worker half of the telemetry contract. tests/fixtures/telemetry/<event>.json
// holds one event of each type exactly as the client builds it, pinned by the
// client's own golden test (tests/unit/test_telemetry_contract.cpp). Here every
// field of each fixture has to be written to Analytics Engine by a column of
// the event's point kinds, or be listed in R2_ONLY as deliberately kept out,
// so a rename on either side fails one of the two tests.
//
// Paths are dotted, with `[]` for the elements of an array; an empty array is
// the single path `name[]` and an empty object its own path. A field matches:
//   - a column path segment for segment, where `*` matches any one segment
//     (map keys such as panel names and overlay ids: `panel_time_sec.*`). For
//     a fallback column only the paths up to the first one the event carries
//     count: the later ones are never read for that event;
//   - an R2_ONLY entry the same way, or as a prefix: an entry keeps its whole
//     subtree in R2 (`tags` covers `tags[].tag`).
// The `event` field is the point's index, not a column.

import { readFileSync, readdirSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { describe, expect, it } from "vitest";
import { mapEventToDataPoints } from "../analytics";
import { EVENT_FANOUT, POINT_KINDS, R2_ONLY, type EventRecord } from "../schema";

const FIXTURE_DIR = fileURLToPath(new URL("../../../../tests/fixtures/telemetry/", import.meta.url));

/** Event types only clients older than schema 3 send, so the client has no fixture for them. */
const OLD_CLIENT_EVENTS = ["feature_adoption"];

const fixtures: Array<[string, EventRecord]> = readdirSync(FIXTURE_DIR)
  .filter((f) => f.endsWith(".json"))
  .sort()
  .map((f) => [f.replace(/\.json$/, ""), JSON.parse(readFileSync(FIXTURE_DIR + f, "utf8"))]);

function leafPaths(value: unknown, prefix: string): string[] {
  if (Array.isArray(value)) {
    const element = `${prefix}[]`;
    if (value.length === 0) return [element];
    return [...new Set(value.flatMap((v) => leafPaths(v, element)))];
  }
  if (value !== null && typeof value === "object") {
    const entries = Object.entries(value);
    if (entries.length === 0) return [prefix];
    return entries.flatMap(([k, v]) => leafPaths(v, prefix ? `${prefix}.${k}` : k));
  }
  return [prefix];
}

/** "changes[].setting" -> ["changes", "[]", "setting"], so `[]` is a segment of its own. */
const segments = (path: string): string[] => path.replace(/\[\]/g, ".[]").split(".");

function matchesFrom(pattern: string[], path: string[]): boolean {
  return pattern.every((seg, i) => i < path.length && (seg === "*" || seg === path[i]));
}

const columnMatches = (pattern: string, path: string): boolean =>
  segments(pattern).length === segments(path).length && matchesFrom(segments(pattern), segments(path));

const r2Covers = (entry: string, path: string): boolean => matchesFrom(segments(entry), segments(path));

/** The column paths this event's fields are actually written from. */
function columnPaths(eventType: string, leaves: string[]): string[] {
  const carried = (p: string) => leaves.some((leaf) => columnMatches(p, leaf));
  return EVENT_FANOUT[eventType].flatMap((kind) =>
    POINT_KINDS[kind].columns.flatMap((c) => {
      if (!c.fallback) return [...c.paths];
      const first = c.paths.findIndex(carried);
      return first < 0 ? [] : c.paths.slice(0, first + 1);
    }),
  );
}

function unaccounted(eventType: string, event: EventRecord): string[] {
  const leaves = leafPaths(event, "");
  const mapped = columnPaths(eventType, leaves);
  const r2 = R2_ONLY[eventType] ?? [];
  return leaves.filter(
    (path) =>
      path !== "event" &&
      !mapped.some((p) => columnMatches(p, path)) &&
      !r2.some((entry) => r2Covers(entry, path)),
  );
}

describe("telemetry contract: client fixtures against the worker mapping", () => {
  it("has a fixture for every event type the client sends", () => {
    const names = fixtures.map(([name]) => name);
    const sent = Object.keys(EVENT_FANOUT).filter((t) => !OLD_CLIENT_EVENTS.includes(t));
    expect(names).toEqual(sent.sort());
    for (const [name, event] of fixtures) expect(event.event, name).toBe(name);
  });

  describe.each(fixtures)("%s", (eventType, event) => {
    it("writes or deliberately keeps in R2 every field", () => {
      expect(unaccounted(eventType, event)).toEqual([]);
    });

    it("lists only R2-only paths the event carries", () => {
      const leaves = leafPaths(event, "");
      const stale = (R2_ONLY[eventType] ?? []).filter((entry) => !leaves.some((p) => r2Covers(entry, p)));
      expect(stale).toEqual([]);
    });

    it("writes at least one point for every kind it fans out to", () => {
      const points = mapEventToDataPoints(event);
      expect(points.length).toBeGreaterThan(0);
      for (const kind of EVENT_FANOUT[eventType]) {
        expect(points.some((p) => p.indexes?.[0] === kind), kind).toBe(true);
      }
    });
  });

  it.each(["update_failed", "update_success"])("%s stores the fixture's target version", (eventType) => {
    const event = fixtures.find(([name]) => name === eventType)![1];
    const [point] = mapEventToDataPoints(event);
    const target = POINT_KINDS[eventType].columns.find((c) => c.field === "version")!;
    expect(target.type).toBe("blob");
    expect(point.blobs![target.slot - 1]).toBe(event.version);
  });
});
