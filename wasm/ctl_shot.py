#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Drive the WASM app through the in-page `helix_ctl` bridge and screenshot each
# screen. Same JSON-RPC vocabulary as `helix-screen ctl`, so a walk written here
# reads like one written against the desktop binary.
#
#   ctl_shot.py <url> <outdir>
import json, os, sys, pathlib
DEBUG = os.environ.get("CTL_DEBUG") == "1"
from playwright.sync_api import sync_playwright

url = sys.argv[1] if len(sys.argv) > 1 else "http://127.0.0.1:8099/index.html"
outdir = pathlib.Path(sys.argv[2] if len(sys.argv) > 2 else "/tmp/wasm-shots")
outdir.mkdir(parents=True, exist_ok=True)

JS_CTL = """(req) => Module.ccall('helix_ctl','string',['string'],[JSON.stringify(req)])"""

def main():
    with sync_playwright() as p:
        b = p.chromium.launch(headless=True, args=[
            "--use-gl=angle", "--use-angle=swiftshader",
            "--enable-unsafe-swiftshader", "--no-sandbox"])
        pg = b.new_page(viewport={"width": 900, "height": 640})
        errors = []
        pg.on("pageerror", lambda e: errors.append(str(e)))
        pg.goto(url, wait_until="commit")
        # Boot is synchronous inside main(); wait for the bridge to answer.
        pg.wait_for_function("() => window.helixReady === true", timeout=180000)

        n = [0]
        def ctl(method, **params):
            n[0] += 1
            try:
                res = pg.evaluate(JS_CTL, {"jsonrpc": "2.0", "id": n[0], "method": method,
                                           "params": params})
            except Exception as exc:  # a wasm trap kills the module, not just the call
                return {"error": {"message": f"TRAP in {method}: {str(exc).splitlines()[0]}"}}
            parsed = json.loads(res)
            if DEBUG:
                print(f"    RAW[{method}] {json.dumps(parsed)[:180]}")
            return parsed

        def shot(name):
            pg.wait_for_timeout(1200)
            path = outdir / f"{name}.png"
            # Full-page, then crop: screenshotting the <canvas> ELEMENT makes
            # Chromium wait for the element to be stable, which parks the rAF
            # main loop mid-frame and leaves LVGL's timer handler re-entrant --
            # the next helix_ctl call then cannot drain the update queue.
            pg.screenshot(path=str(path), clip={"x": 0, "y": 80, "width": 900, "height": 480})
            print(f"  shot -> {path}")

        def step(label, fn):
            r = fn()
            err = r.get("error")
            print(f"{label}: {'ERR ' + json.dumps(err) if err else 'ok'}")
            return r

        # Resolve by explicit PATH, never by a bare name that several panels
        # share: `resolve` on an ambiguous name currently traps the module
        # (see wasm/README.md, Known issues).
        HDR = ("s/ams_overview_panel/overview_content/overlay_header/"
               "back_button/detail_unit_name")
        BACK = "s/ams_overview_panel/overview_content/overlay_header/back_button"

        def header():
            return ctl("text", path=HDR).get("result", {}).get("text")

        step("ping", lambda: ctl("ping"))
        print("  where:", json.dumps(ctl("get_current").get("result", {})))
        shot("01-multi-filament")

        step("open SnapSwap", lambda: ctl("click", name="ams_unit_card[0]"))
        print("  header:", header())
        shot("02-snapswap")

        step("back to cards", lambda: ctl("click", path=BACK))
        step("open ACE 1", lambda: ctl("click", name="ams_unit_card[1]"))
        print("  header:", header())
        shot("03-multiace")

        step("back to cards", lambda: ctl("click", path=BACK))
        print("  where:", json.dumps(ctl("get_current").get("result", {})))
        shot("04-back-at-cards")

        if errors:
            print("PAGE ERRORS:", *errors, sep="\n  ")
        b.close()

main()
