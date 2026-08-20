#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Watch the distance-proportional filament fill in the browser build.
#
# Samples the ACE page repeatedly THROUGH a load instead of at the end of it,
# because the thing under test is the motion, not the final frame — a single
# "loaded" screenshot looks identical whether the tube filled gradually or
# snapped. Also captures the widget's own log line, which is what says the ramp
# was timed against the backend's bowden figure rather than the fallback.
#
#   fill_shot.py <url> <outdir>
import json, re, sys, pathlib
from playwright.sync_api import sync_playwright

url = sys.argv[1] if len(sys.argv) > 1 else "http://127.0.0.1:8080/index.html"
outdir = pathlib.Path(sys.argv[2] if len(sys.argv) > 2 else "/tmp/wasm-fill")
outdir.mkdir(parents=True, exist_ok=True)
JS = """(req) => Module.ccall('helix_ctl','string',['string'],[JSON.stringify(req)])"""

# Samples across the feed phase. SCRIPT_FEED_MS is 4000 ms, so a sample every
# 700 ms puts several frames inside the fill itself.
SAMPLE_MS = int(__import__('os').environ.get('FILL_SAMPLE_MS', 700))
SAMPLES = int(__import__('os').environ.get('FILL_SAMPLES', 12))

def main():
    with sync_playwright() as p:
        b = p.chromium.launch(headless=True, args=[
            "--use-gl=angle", "--use-angle=swiftshader",
            "--enable-unsafe-swiftshader", "--no-sandbox"])
        pg = b.new_page(viewport={"width": 900, "height": 640})
        logs, errs = [], []
        pg.on("console", lambda m: logs.append(m.text))
        pg.on("pageerror", lambda e: errs.append(str(e)))
        pg.goto(url, wait_until="commit")
        pg.wait_for_function("() => window.helixReady === true", timeout=180000)

        n = [0]
        def ctl(method, **params):
            n[0] += 1
            try:
                return json.loads(pg.evaluate(JS, {"jsonrpc": "2.0", "id": n[0],
                                                   "method": method, "params": params}))
            except Exception as exc:
                return {"error": {"message": f"TRAP in {method}: {str(exc).splitlines()[0]}"}}

        def shot(name):
            pg.screenshot(path=str(outdir / f"{name}.png"),
                          clip={"x": 0, "y": 80, "width": 900, "height": 480})

        def do(label, method, **params):
            r = ctl(method, **params)
            err = r.get("error")
            print(f"  {label}: {'ERR ' + json.dumps(err)[:120] if err else 'ok'}")
            return r

        print("Opening the multiACE page")
        do("open ACE 1", "click", name="ams_unit_card[1]")
        shot("00-idle")

        print("LOAD (ACE 1 bay 1 -> head 0, which already holds bay 1 => swap)")
        do("tap bay 3", "click", name="ams_slot_view[2]")
        do("press Load", "click", name="btn_load")

        for i in range(SAMPLES):
            pg.wait_for_timeout(SAMPLE_MS)
            shot(f"{i+1:02d}-t{(i+1)*SAMPLE_MS:05d}ms")
        print(f"  captured {SAMPLES} frames at {SAMPLE_MS} ms spacing")

        # The whole point of the exercise: does the ramp coincide with the step
        # that moves filament? Print both timelines against one clock.
        print("\n--- phase vs fill timeline (ms from Load press) ---")
        t0 = None
        for line in logs:
            m = re.search(r"Backend phase (-?\d+)", line)
            f = re.search(r"Fill (LOAD|UNLOAD) (\d+)", line)
            ts = re.search(r"\((\d+\.\d+),", line)
            if not (m or f):
                continue
            stamp = float(ts.group(1)) * 1000 if ts else None
            if m:
                print(f"  phase {m.group(1)}")
            if f:
                print(f"  >>> FILL {f.group(1)} starts")

        # The two lines that prove the wiring: the backend's parsed bowden
        # figures, and the ramp actually timed against them.
        print("\n--- feed kinematics / fill log ---")
        for line in logs:
            if re.search(r"ACE \d+ feed:|Fill (LOAD|UNLOAD)|Fill animation|Fill resync|Segment changed|FillDbg", line):
                print("  " + line.strip())

        if errs:
            print("\nPAGE ERRORS:", *errs, sep="\n  ")
        b.close()

main()
