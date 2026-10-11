# SPDX-License-Identifier: GPL-3.0-or-later
"""scripts/esp32_serial_snapshot.py: parsing a serial dump and the RGB565 -> PNG step."""
import base64
import importlib.util
import struct
import zlib
from pathlib import Path

import pytest

SCRIPT = Path(__file__).resolve().parents[2] / "scripts" / "esp32_serial_snapshot.py"
spec = importlib.util.spec_from_file_location("snap", SCRIPT)
snap = importlib.util.module_from_spec(spec)
spec.loader.exec_module(snap)


def dump_lines(pixels: bytes, w: int, h: int, noise: bool = True) -> list[str]:
    comp = zlib.compressobj(6, zlib.DEFLATED, -15)
    data = comp.compress(pixels) + comp.flush()
    chunks = [data[i:i + 57] for i in range(0, len(data), 57)]
    lines = ["I (100) app_boot: boot noise",
             f"=====HELIX-SNAP {w} {h} RGB565 DEFLATE {len(data)} {zlib.crc32(data):08x} {len(chunks)}"]
    for seq, chunk in enumerate(chunks):
        lines.append(snap_line(seq, chunk))
        if noise:
            lines.append("W (200) lvgl_glue: a log line from another task")
    lines.append(f"=====HELIX-SNAP-END {len(data)}")
    return lines


def snap_line(seq: int, chunk: bytes) -> str:
    crc = zlib.crc32(chunk, zlib.crc32(f"{seq} ".encode()))
    return f"SNAP:{seq} {crc:08x} {base64.b64encode(chunk).decode()}"


def decode(lines: list[str]) -> tuple[int, int, bytes]:
    w, h, data, missing = snap.parse_dump(lines)
    assert missing == []
    return w, h, snap.decode_pixels(w, h, data)


PIXELS = struct.pack("<6H", 0xF800, 0x07E0, 0x001F, 0xFFFF, 0x0000, 0x8410)
NOISY = bytes(range(256)) * 40  # incompressible enough to span many lines


def test_a_dump_with_interleaved_logs_round_trips():
    assert decode(dump_lines(PIXELS, 3, 2)) == (3, 2, PIXELS)


def test_rgb565_expands_to_full_range_rgb():
    png = snap.rgb565_to_png(struct.pack("<2H", 0xF800, 0xFFFF), 2, 1)
    idat = png[png.index(b"IDAT") + 4:]
    rows = zlib.decompress(idat[:struct.unpack(">I", png[png.index(b"IDAT") - 4:png.index(b"IDAT")])[0]])
    assert rows == bytes([0, 255, 0, 0, 255, 255, 255])


def test_a_log_message_inside_a_line_marks_that_line_missing():
    lines = dump_lines(NOISY, 80, 64, noise=False)
    lines[4] = lines[4][:40] + "E (5762512) task_wdt: Task watchdog got triggered."
    assert snap.parse_dump(lines)[3] == [2]


def test_a_log_prefix_in_front_of_a_line_is_skipped():
    lines = dump_lines(NOISY, 80, 64, noise=False)
    lines[4] = "I (10462) wifi:" + lines[4]
    assert decode(lines) == (80, 64, NOISY)


def test_a_line_corrupted_into_valid_base64_fails_its_crc():
    lines = dump_lines(NOISY, 80, 64, noise=False)
    b64 = lines[5][-8:]
    lines[5] = lines[5][:-8] + ("A" if b64[0] != "A" else "B") + b64[1:]
    assert snap.parse_dump(lines)[3] == [3]


def test_a_damaged_sequence_number_fails_the_line():
    lines = dump_lines(NOISY, 80, 64, noise=False)
    lines[4] = lines[4].replace("SNAP:2 ", "SNAP:7 ", 1)  # line 2 now claims to be line 7
    assert snap.parse_dump(lines)[3] == [2]


def test_a_resent_line_fills_the_gap():
    lines = dump_lines(NOISY, 80, 64, noise=False)
    good = lines[4]
    lines[4] = good[:30] + "I (1) wifi: noise"
    lines.append("W (300) lvgl_glue: noise after the dump")
    lines.append(good)
    assert decode(lines) == (80, 64, NOISY)


def test_a_truncated_dump_reports_the_lines_it_lacks():
    lines = dump_lines(NOISY, 80, 64, noise=False)
    count = int(lines[1].split()[-1])
    del lines[-3:]  # the last two payload lines and the end marker never arrived
    assert snap.parse_dump(lines)[3] == [count - 2, count - 1]


def test_a_payload_shorter_than_its_header_is_refused():
    lines = dump_lines(NOISY, 80, 64, noise=False)
    fields = lines[1].split()
    fields[5] = str(int(fields[5]) + 1)
    lines[1] = " ".join(fields)
    with pytest.raises(ValueError, match="truncated"):
        snap.parse_dump(lines)


def test_an_old_firmware_header_is_refused_by_name():
    with pytest.raises(ValueError, match="firmware too old"):
        snap.parse_dump(["=====HELIX-SNAP 3 2 RGB565 DEFLATE", "SNAP:AAAA"])


def test_a_firmware_error_is_reported():
    with pytest.raises(ValueError, match="no memory"):
        snap.parse_dump(["=====HELIX-SNAP-ERROR no memory for the snapshot"])


class FakePort:
    """Answers "snap" with the next scripted dump and "snapline N" from replies[N]."""

    def __init__(self, dumps, replies=None):
        self.dumps, self.replies, self.out, self.writes = list(dumps), replies or {}, b"", []

    def write(self, data):
        cmd = data.decode().strip()
        self.writes.append(cmd)
        if cmd == "snap" and self.dumps:
            self.out += ("\n".join(self.dumps.pop(0)) + "\n").encode()
        elif cmd.startswith("snapline "):
            queue = self.replies.get(int(cmd.split()[1]), [])
            if queue and (reply := queue.pop(0)):
                self.out += (reply + "\n").encode()

    def read(self, n):
        data, self.out = self.out[:n], self.out[n:]
        return data


def capture(port, **kw):
    return snap.capture(port, timeout=1.0, line_wait=0.05, **kw)


def test_a_damaged_header_gets_one_fresh_snap():
    good = dump_lines(NOISY, 80, 64)
    bad = list(good)
    bad[1] = bad[1][:25] + "E (1) task_wdt: Task watchdog got triggered."
    port = FakePort([bad, good])
    w, h, data = capture(port)
    assert snap.decode_pixels(w, h, data) == NOISY
    assert port.writes.count("snap") == 2


def test_a_second_unreadable_dump_fails():
    bad = dump_lines(NOISY, 80, 64)[2:]  # no header at all
    port = FakePort([bad, bad, dump_lines(NOISY, 80, 64)])
    with pytest.raises(ValueError, match="no dump header"):
        capture(port)
    assert port.writes.count("snap") == 2


def test_a_damaged_line_is_resent_and_the_dump_decodes():
    lines = dump_lines(NOISY, 80, 64, noise=False)
    good = lines[5]
    lines[5] = good[:30] + "I (1) wifi: noise"
    port = FakePort([lines], {3: [None, good]})  # first resend lost, second arrives
    w, h, data = capture(port)
    assert snap.decode_pixels(w, h, data) == NOISY
    assert port.writes.count("snapline 3") == 2


def test_resends_stop_at_the_overall_cap_and_name_the_lines():
    lines = dump_lines(NOISY, 80, 64, noise=False)
    count = int(lines[1].split()[-1])
    damaged = list(range(count))
    for seq in damaged:
        lines[2 + seq] = "SNAP:garbage"
    port = FakePort([lines])
    with pytest.raises(ValueError, match=r"SNAP line\(s\) \[0, 1, 2, .*after 10 resend"):
        capture(port)
    assert sum(w.startswith("snapline") for w in port.writes) == 10


def test_resends_stop_when_the_window_closes():
    lines = dump_lines(NOISY, 80, 64, noise=False)
    lines[2] = "SNAP:garbage"
    lines[3] = "SNAP:garbage"
    port = FakePort([lines])
    with pytest.raises(ValueError, match=r"\[0, 1\]"):
        capture(port, resend_window=0.0)
    assert not any(w.startswith("snapline") for w in port.writes)
