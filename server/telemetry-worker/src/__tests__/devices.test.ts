// SPDX-License-Identifier: GPL-3.0-or-later
import { describe, it, expect } from "vitest";
import { buildDeviceList, buildDevicePage, isDeviceId } from "../devices";
import { deviceQueries, devicesQueries, filterFor } from "../queries";
import { EVENT_FANOUT, POINT_KINDS, col, decodeRow } from "../schema";

const ID = "a".repeat(32) + "0123456789abcdef".repeat(2);

describe("isDeviceId", () => {
  it("accepts 64 lowercase hex characters", () => {
    expect(isDeviceId(ID)).toBe(true);
  });
  it.each([
    ["uppercase", ID.toUpperCase().replace(/0X/, "0x")],
    ["63 chars", ID.slice(1)],
    ["65 chars", ID + "a"],
    ["empty", ""],
    ["injection", "abc' OR 1=1"],
    ["trailing newline", ID + "\n"],
  ])("rejects %s", (_n, id) => {
    expect(isDeviceId(id)).toBe(false);
  });
});

describe("device_id slot", () => {
  it("is blob1 for every event kind, the slot deviceQueries filters on", () => {
    for (const kinds of Object.values(EVENT_FANOUT)) {
      for (const k of kinds) expect(col(k, "device_id")).toBe("blob1");
    }
  });
});

describe("deviceQueries", () => {
  it("throws on a bad id", () => {
    expect(() => deviceQueries("abc' OR 1=1")).toThrow();
    expect(() => deviceQueries(ID.toUpperCase().replace(/0X/, "0x") + "G")).toThrow();
  });
  it("filters the timeline on the id, over 90 days, newest first", () => {
    const [timeline, versions] = deviceQueries(ID);
    expect(timeline).toContain(`blob1 = '${ID}'`);
    expect(timeline).toContain("INTERVAL '90' DAY");
    expect(timeline).toContain("ORDER BY timestamp DESC");
    expect(timeline).toContain("LIMIT 500");
    expect(timeline).toContain("blob20");
    expect(timeline).toContain("double20");
    expect(versions).toContain(col("session", "app_version"));
  });
});

describe("devicesQueries", () => {
  it("puts the session filter into the list and total queries", () => {
    const q = devicesQueries(30, { platform: "pi" });
    const f = filterFor("session", { platform: "pi" });
    expect(q).toHaveLength(4);
    expect(q[0]).toContain(f);
    expect(q[1]).toContain(f);
    expect(q[2]).toContain(filterFor("crash", { platform: "pi" }));
    expect(q[3]).toContain(filterFor("print_outcome", { platform: "pi" }));
    expect(q[0]).toMatch(/ORDER BY last_seen DESC\s+LIMIT 200/);
  });
});

describe("decodeRow", () => {
  it("reads each column from its slot and keeps empties and zeros", () => {
    const d = decodeRow("crash", { blob1: ID, blob3: "SIGSEGV", double1: 325 });
    expect(d.signal_name).toBe("SIGSEGV");
    expect(d.uptime_sec).toBe(325);
    expect(d.fault_addr).toBe("");
    expect(d.signal).toBe(0);
    expect(Object.keys(d)).toHaveLength(POINT_KINDS.crash.columns.length);
  });
  it("throws on an unknown kind", () => {
    expect(() => decodeRow("nope", {})).toThrow();
  });
});

describe("buildDeviceList", () => {
  it("joins crashes and prints by id and zero-fills the rest", () => {
    const sessions = {
      data: [
        { device_id: "d1", last_seen: "2026-10-02 00:00:00", version: "1.0.0", platform: "pi", board: "", model: "V2", sessions: 5 },
        { device_id: "d2", last_seen: "2026-10-01 00:00:00", version: "1.0.0", platform: "pi", board: "", model: "V2", sessions: 2 },
      ],
    };
    const r = buildDeviceList(sessions, { data: [{ device_id: "d1", crashes: 3 }, { device_id: "gone", crashes: 9 }] }, { data: [{ device_id: "d2", prints: 7 }] }, { data: [{ total: 450 }] });
    expect(r.total).toBe(450);
    expect(r.devices.map((d) => [d.device_id, d.crashes, d.prints])).toEqual([["d1", 3, 0], ["d2", 0, 7]]);
  });
  it("handles empty results", () => {
    expect(buildDeviceList({}, undefined, null)).toEqual({
      total: 0,
      devices: [],
      not_applicable: { crashes: null, prints: null },
    });
  });
  it("carries the columns a filter leaves unreported", () => {
    const na = { crashes: "model", prints: "model" } as const;
    expect(buildDeviceList({}, undefined, null, undefined, na).not_applicable).toEqual(na);
  });
});

describe("buildDevicePage", () => {
  const row = (kind: string, ts: string, slots: Record<string, unknown>) => ({ timestamp: ts, index1: kind, blob1: ID, ...slots });
  const rows = {
    data: [
      row("crash", "2026-10-03 00:00:00", { blob2: "1.0.0", blob3: "SIGSEGV", double1: 12 }),
      row("settings_snapshot", "2026-10-02 12:00:00", { blob2: "1.0.0", blob3: "pi", double1: 80 }),
      row("settings_snapshot", "2026-10-01 12:00:00", { blob2: "0.9.0", blob3: "pi", double1: 50 }),
      row("hardware_profile", "2026-10-01 00:00:00", { blob2: "0.9.0", blob3: "pi", blob4: "Voron 2.4", double1: 1 }),
    ],
  };
  const versions = {
    data: [
      { version: "0.9.0", first_seen: "2026-09-20 00:00:00", last_seen: "2026-10-01 00:00:00" },
      { version: "1.0.0-beta.9", first_seen: "2026-10-01 00:00:00", last_seen: "2026-10-01 20:00:00" },
      { version: "1.0.0-beta.10", first_seen: "2026-10-01 20:00:00", last_seen: "2026-10-02 00:00:00" },
    ],
  };

  it("returns null with no rows", () => {
    expect(buildDevicePage(ID, { data: [] }, versions)).toBeNull();
    expect(buildDevicePage(ID, undefined, undefined)).toBeNull();
  });
  it("summarises identity, latest hardware and settings, and sorted versions", () => {
    const p = buildDevicePage(ID, rows, versions)!;
    expect(p.device_id).toBe(ID);
    expect(p.model).toBe("Voron 2.4");
    expect(p.platform).toBe("pi");
    expect(p.version).toBe("1.0.0");
    expect(p.first_seen).toBe("2026-09-20 00:00:00");
    expect(p.last_seen).toBe("2026-10-03 00:00:00");
    expect(p.hardware?.printer_model).toBe("Voron 2.4");
    expect(p.settings?.brightness_pct).toBe(80);
    expect(p.versions.map((v) => v.version)).toEqual(["1.0.0-beta.10", "1.0.0-beta.9", "0.9.0"]);
  });
  it("drops device_id and empty or zero fields from the hardware and settings tables", () => {
    const p = buildDevicePage(ID, rows, versions)!;
    for (const table of [p.hardware!, p.settings!]) {
      expect(table).not.toHaveProperty("device_id");
      expect(Object.values(table).filter((v) => v === "" || v === 0)).toEqual([]);
    }
    expect(p.hardware).toEqual({ app_version: "0.9.0", app_platform: "pi", printer_model: "Voron 2.4", extruder_count: 1 });
  });
  it("lists newest first with empty and zero fields and device_id dropped", () => {
    const p = buildDevicePage(ID, rows, versions)!;
    expect(p.timeline.map((t) => t.kind)).toEqual(["crash", "settings_snapshot", "settings_snapshot", "hardware_profile"]);
    expect(p.timeline[0].fields).toEqual({ app_version: "1.0.0", signal_name: "SIGSEGV", uptime_sec: 12 });
  });
});
