#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Screenshot an ESP32 panel over its serial console.

Sends "snap"; the firmware (firmware/helixscreen-esp32/main/serial_snapshot.c)
answers with the active screen as raw-deflated RGB565, base64 on numbered "SNAP:"
lines, each with its own crc32, between HELIX-SNAP markers; the header carries the
payload's size and crc32. Log lines interleave between them and are ignored; a
line a ROM message split is caught by its crc and fetched again with "snapline N".

    esp32_serial_snapshot.py /dev/ttyUSB0 out.png [--timeout 120] [--settle 45]
        [--tap X,Y ...] [--tap-wait 1.5]

--tap sends "tap X Y" (panel coordinates) before the screenshot, in order,
pausing --tap-wait seconds after each so the UI settles. --notes prints every
notification since boot (the toasts the bell counts) instead of a screenshot.

Needs pyserial. A board wired for auto-reset resets while RTS and DTR differ;
after opening, RTS is released before DTR so they never do. The request is
repeated until the dump starts, so a reset that happens anyway costs one boot,
not the capture, and --settle waits for a screen that has finished booting.
"""

import argparse
import base64
import struct
import sys
import time
import zlib


def rgb565_to_png(raw: bytes, width: int, height: int) -> bytes:
    rows = bytearray()
    for y in range(height):
        rows.append(0)  # filter: none
        line = raw[y * width * 2:(y + 1) * width * 2]
        for (px,) in struct.iter_unpack("<H", line):
            rows += bytes(((px >> 11) << 3 | px >> 13, (px >> 5 & 0x3F) << 2 | (px >> 9 & 3),
                           (px & 0x1F) << 3 | (px >> 2 & 7)))

    def chunk(tag: bytes, data: bytes) -> bytes:
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data))

    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(bytes(rows), 6)) + chunk(b"IEND", b""))


def parse_dump(lines: list[str]) -> tuple[int, int, bytes, list[int]]:
    """(width, height, deflated payload or b"", missing line numbers) of the last
    dump in lines. Lines that fail their crc count as missing. Raises ValueError."""
    header = None
    for line in lines:
        if line.startswith("=====HELIX-SNAP-ERROR"):
            raise ValueError(line)
        if line.startswith("=====HELIX-SNAP "):
            fields = line.split()
            if len(fields) != 8:
                raise ValueError(f"unsupported dump header (firmware too old?): {line}")
            _, w, h, fmt, enc, total, crc, count = fields
            if (fmt, enc) != ("RGB565", "DEFLATE"):
                raise ValueError(f"unsupported dump: {fmt} {enc}")
            header = (int(w), int(h), int(total), int(crc, 16), int(count))
            chunks = {}
        elif "SNAP:" in line and header is not None:
            seq, chunk = parse_line(line)
            if seq is not None and seq < header[4]:
                chunks[seq] = chunk
    if header is None:
        raise ValueError("no dump header")
    w, h, total, crc, count = header
    missing = [i for i in range(count) if i not in chunks]
    if missing:
        return w, h, b"", missing
    data = b"".join(chunks[i] for i in range(count))
    if len(data) != total:
        raise ValueError(f"truncated: {len(data)} of {total} bytes")
    if zlib.crc32(data) != crc:
        raise ValueError("payload crc mismatch")
    return w, h, data, []


def parse_line(line: str) -> tuple[int | None, bytes]:
    """(sequence number, payload) of one SNAP line, or (None, b"") if it is damaged.
    A log written in pieces can leave its prefix in front of the line."""
    try:
        seq, crc, b64 = line[line.index("SNAP:") + 5:].strip().split(" ")
        chunk = base64.b64decode(b64, validate=True)
        if zlib.crc32(chunk, zlib.crc32(f"{seq} ".encode())) == int(crc, 16):
            return int(seq), chunk
    except ValueError:
        pass
    return None, b""


def decode_pixels(width: int, height: int, data: bytes) -> bytes:
    raw = zlib.decompress(data, -15)
    if len(raw) != width * height * 2:
        raise ValueError(f"expected {width * height * 2} pixel bytes, got {len(raw)}")
    return raw


def read_lines(port, buf: bytes) -> tuple[list[str], bytes]:
    buf += port.read(4096)
    *done, buf = buf.split(b"\n")
    return [d.decode("ascii", "replace").rstrip("\r") for d in done], buf


def request_dump(port, timeout: float) -> list[str]:
    """Ask for a snap, repeating until one starts, and read until it ends."""
    lines, buf, next_ask, deadline = [], b"", time.time(), time.time() + timeout
    while time.time() < deadline:
        started = any(l.startswith("=====HELIX-SNAP") for l in lines)
        if not started and time.time() >= next_ask:
            port.write(b"\nsnap\n")
            next_ask = time.time() + 2.0
        got, buf = read_lines(port, buf)
        lines += got
        if any(l.startswith(("=====HELIX-SNAP-END", "=====HELIX-SNAP-ERROR")) for l in got):
            break
    return lines


def resend(port, seq: int, wait: float, lines: list[str]) -> bool:
    """Ask for line seq again; True once it arrives intact. Appends what is read."""
    port.write(f"\nsnapline {seq}\n".encode())
    buf, until = b"", time.time() + wait
    while time.time() < until:
        got, buf = read_lines(port, buf)
        lines += got
        if any(parse_line(l)[0] == seq for l in got if "SNAP:" in l):
            return True
    return False


def capture(port, timeout: float, line_wait: float = 2.0, max_resends: int = 10,
            resend_window: float = 30.0) -> tuple[int, int, bytes]:
    """(width, height, deflated payload) of one verified dump; raises ValueError.

    An unreadable dump (no or damaged header, bad payload) is asked for once more.
    Damaged lines are asked for again by number from the dump the board kept, up
    to 3 times each and max_resends requests or resend_window seconds in all."""
    for attempt in (1, 2):
        lines = request_dump(port, timeout)
        try:
            width, height, data, missing = parse_dump(lines)
            break
        except ValueError as e:
            if attempt == 2 or str(e).startswith("=====HELIX-SNAP-ERROR"):
                raise
            print(f"unreadable dump ({e}); asking for a fresh one", file=sys.stderr)
    sent, failed, stop = 0, [], time.time() + resend_window
    for seq in missing:
        for _ in range(3):
            if sent >= max_resends or time.time() >= stop:
                failed.append(seq)
                break
            sent += 1
            if resend(port, seq, line_wait, lines):
                break
        else:
            failed.append(seq)
    if failed:
        raise ValueError(f"SNAP line(s) {failed} stayed damaged or missing after {sent} resend(s)")
    if missing:
        print(f"resent SNAP line(s) {missing}", file=sys.stderr)
        width, height, data, _ = parse_dump(lines)
    return width, height, data


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("port")
    ap.add_argument("out", nargs="?", help="PNG to write (not needed with --notes)")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--timeout", type=float, default=120.0)
    ap.add_argument("--settle", type=float, default=0.0,
                    help="seconds to wait after opening the port before asking")
    ap.add_argument("--tap", action="append", default=[], metavar="X,Y",
                    help="tap at X,Y before the screenshot; repeatable")
    ap.add_argument("--tap-wait", type=float, default=1.5)
    ap.add_argument("--notes", action="store_true", help="print the notification history")
    ap.add_argument("--cmd", action="append", default=[], metavar="TEXT",
                    help="console command to send after the taps; repeatable")
    ap.add_argument("--log", help="append every line the board prints to this file")
    args = ap.parse_args()
    if not args.notes and not args.out:
        ap.error("out is required unless --notes is given")

    import serial

    port = serial.Serial()
    port.port, port.baudrate, port.timeout = args.port, args.baud, 0.5
    # The CH340 board resets while RTS and DTR differ. Opening raises both (no
    # reset); dropping RTS first keeps them from ever disagreeing. Presetting
    # them before open() would not: pyserial applies DTR before RTS there.
    port.open()
    port.rts = False
    port.dtr = False
    port.reset_input_buffer()

    wait = time.sleep
    if args.log:
        log = open(args.log, "a")
        raw_read = port.read

        def logged_read(n):
            data = raw_read(n)
            log.write(data.decode("utf-8", "replace"))
            log.flush()
            return data

        port.read = logged_read

        def wait(secs):  # keep reading, so the OS buffer cannot drop boot output
            until = time.time() + secs
            while time.time() < until:
                port.read(4096)

    wait(args.settle)
    for tap in args.tap:
        x, y = (int(v) for v in tap.split(","))
        port.write(f"\ntap {x} {y}\n".encode())
        wait(args.tap_wait)
    for cmd in args.cmd:
        port.write(f"\n{cmd}\n".encode())
        wait(args.tap_wait)
    lines, buf, deadline = [], b"", time.time() + args.timeout
    if args.notes:
        port.write(b"\nnotes\n")
        while time.time() < deadline:
            buf += port.read(4096)
            *done, buf = buf.split(b"\n")
            lines += [d.decode("utf-8", "replace").rstrip("\r") for d in done]
            if any(l.startswith("=====HELIX-NOTES-END") for l in lines):
                break
        notes = [l[len("NOTE: "):] for l in lines if l.startswith("NOTE: ")]
        print("\n".join(notes) if notes else "(no notifications)")
        return 0
    try:
        width, height, data = capture(port, args.timeout)
    except ValueError as e:
        raise SystemExit(str(e))
    raw = decode_pixels(width, height, data)
    with open(args.out, "wb") as f:
        f.write(rgb565_to_png(raw, width, height))
    print(f"{args.out}: {width}x{height}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
