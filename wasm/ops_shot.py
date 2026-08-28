#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Exercise load / unload / swap in the browser build and screenshot each phase.
# Every command goes through the same JSON-RPC surface as `helix-screen ctl`, and
# every reaction comes from the real AmsBackendMultiAce reading scripted firmware
# frames — so a green run here is evidence about the app, not about the harness.
#
#   ops_shot.py <url> <outdir>
import json, sys, pathlib
from playwright.sync_api import sync_playwright

url = sys.argv[1] if len(sys.argv) > 1 else "http://127.0.0.1:8099/index.html"
outdir = pathlib.Path(sys.argv[2] if len(sys.argv) > 2 else "/tmp/wasm-ops")
outdir.mkdir(parents=True, exist_ok=True)
JS = """(req) => Module.ccall('helix_ctl','string',['string'],[JSON.stringify(req)])"""

def main():
    with sync_playwright() as p:
        b = p.chromium.launch(headless=True, args=[
            "--use-gl=angle", "--use-angle=swiftshader",
            "--enable-unsafe-swiftshader", "--no-sandbox"])
        pg = b.new_page(viewport={"width": 900, "height": 640})
        errs = []
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
            pg.wait_for_timeout(400)
            pg.screenshot(path=str(outdir / f"{name}.png"),
                          clip={"x": 0, "y": 80, "width": 900, "height": 480})
            print(f"    shot -> {name}.png")

        def do(label, method, **params):
            r = ctl(method, **params)
            err = r.get("error")
            print(f"  {label}: {'ERR ' + json.dumps(err)[:110] if err else 'ok'}")
            return r

        def status_text():
            for name in ("status_text", "operation_status", "status_label"):
                r = ctl("text", name=name).get("result", {}).get("text")
                if r:
                    return r
            return "?"

        # --- swap: a different ACE bay onto a head that already holds filament --
        # The interesting one. AmsBackendMultiAce recognises this as a bay swap
        # and emits ACE_UNLOAD_HEAD + ACE_LOAD_HEAD back to back, so one user
        # gesture runs both directions inside a single LOAD_SWAP step bar.
        print("SWAP (ACE 1 bay 3 -> head 0, which already holds bay 1)")
        do("open ACE 1", "click", name="ams_unit_card[1]")
        shot("01-ace-idle")
        do("tap bay 3", "click", name="ams_slot_view[2]")
        do("press Load", "click", name="btn_load")
        pg.wait_for_timeout(4000)
        print("    status:", status_text())
        shot("02-swap-unload-half")
        pg.wait_for_timeout(7000)
        print("    status:", status_text())
        shot("03-swap-load-half")
        pg.wait_for_timeout(8000)
        print("    status:", status_text())
        shot("04-swap-done")

        # --- unload, on the same ACE-fed head --------------------------------
        print("UNLOAD (ACE-fed head — ends at preload_finish, the ACE does the retract)")
        do("back to cards", "click", name="back_button")
        do("open SnapSwap", "click", name="ams_unit_card[0]")
        do("press Unload", "click", name="btn_unload")
        pg.wait_for_timeout(3200)
        print("    status:", status_text())
        shot("05-unloading")
        pg.wait_for_timeout(6000)
        print("    status:", status_text())
        shot("06-after-unload")

        # --- load, back onto the now-empty head ------------------------------
        # From the ACE, because that is where the filament is: an empty ACE-fed
        # head has nothing of its own to load, so the menu offers no Load there.
        print("LOAD (ACE 1 bay 1 -> the now-empty head 0)")
        do("back to cards", "click", name="back_button")
        do("open ACE 1", "click", name="ams_unit_card[1]")
        do("tap bay 1", "click", name="ams_slot_view[0]")
        do("press Load", "click", name="btn_load")
        pg.wait_for_timeout(4000)
        print("    status:", status_text())
        shot("07-loading")
        pg.wait_for_timeout(7000)
        print("    status:", status_text())
        shot("08-after-load")

        if errs:
            print("PAGE ERRORS:", *errs, sep="\n  ")
        b.close()

main()
