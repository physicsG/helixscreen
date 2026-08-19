#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Capture the ACE page across a mocked load run: idle -> mid-load -> loaded.
import sys
from playwright.sync_api import sync_playwright

url = sys.argv[1]
outdir = sys.argv[2]

with sync_playwright() as p:
    b = p.chromium.launch(headless=True, args=[
        "--use-gl=angle", "--use-angle=swiftshader",
        "--enable-unsafe-swiftshader", "--no-sandbox"])
    pg = b.new_page(viewport={"width": 960, "height": 700})
    msgs = []
    pg.on("console", lambda m: msgs.append(f"[{m.type}] {m.text}"))
    pg.on("pageerror", lambda e: msgs.append(f"[err] {e}"))
    pg.goto(url, wait_until="load")
    pg.wait_for_timeout(2500)
    pg.screenshot(path=f"{outdir}/ace_idle.png")
    try:
        pg.evaluate("Module._ace_load()")
        trig = "ok"
    except Exception as e:
        trig = f"FAILED: {e}"
    pg.wait_for_timeout(7000)
    pg.screenshot(path=f"{outdir}/ace_loading.png")
    pg.wait_for_timeout(8000)
    pg.screenshot(path=f"{outdir}/ace_loaded.png")
    print("trigger:", trig)
    for m in msgs[-15:]:
        print(m)
    b.close()
