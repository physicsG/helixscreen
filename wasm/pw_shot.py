#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Headless screenshot of a WASM/LVGL page for autonomous verification.
#   pw_shot.py <url> <out.png> [wait_ms]
import sys
from playwright.sync_api import sync_playwright

url = sys.argv[1] if len(sys.argv) > 1 else "http://127.0.0.1:8042/index.html"
out = sys.argv[2] if len(sys.argv) > 2 else "/tmp/shot.png"
wait_ms = int(sys.argv[3]) if len(sys.argv) > 3 else 4500

with sync_playwright() as p:
    browser = p.chromium.launch(
        headless=True,
        args=[
            "--use-gl=angle", "--use-angle=swiftshader",
            "--enable-unsafe-swiftshader", "--no-sandbox",
        ],
    )
    page = browser.new_page(viewport={"width": 900, "height": 640})
    msgs = []
    page.on("console", lambda m: msgs.append(f"[{m.type}] {m.text}"))
    page.on("pageerror", lambda e: msgs.append(f"[pageerror] {e}"))
    page.goto(url, wait_until="load")
    page.wait_for_timeout(wait_ms)  # let LVGL render several frames
    page.screenshot(path=out)
    print("SCREENSHOT", out)
    for m in msgs[-40:]:
        print(m)
    browser.close()
