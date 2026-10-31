# SPDX-License-Identifier: GPL-3.0-or-later
"""
Print-start hooks for prints started anywhere: the HelixPrint plugin's
`print_hooks` feature.

The screen keeps a standing options record in Moonraker's database,
`helix-screen` / `standing_print_options`: finished G-code rendered by the
same functions the screen's own print start uses. When history adds a job
that no HelixScreen path started, and the user opted in, this module sends
that G-code once. It never renders backend G-code itself.

Everything above PrintHooks is pure and touches no Moonraker object.
Python 3.8, stdlib only.
"""

from __future__ import annotations

import logging
from typing import Any, Dict, List, Optional, Tuple

PRINT_HOOKS_FEATURE = "print_hooks"
PRINT_HOOKS_FEATURE_VERSION = 1

# Where the screen keeps the record: namespace helix-screen, this key.
STANDING_OPTIONS_KEY = "standing_print_options"

# The record's shape this module reads: {"version": 1, "gcode": "..."}
STANDING_OPTIONS_VERSION = 1

# The marker kind for a start the screen made itself, private to this module:
# the screen sends its own options with such a start.
SCREEN_START_KIND = "screen_start"

# How long a screen start waits for history's job. The start command can wait
# behind Klipper's gcode queue for as long as the app waits for it (10
# minutes), and a start that fails is withdrawn instead of left to expire.
SCREEN_START_TTL_S = 600.0

# How many job starts are remembered to keep a repeated `added` from
# sending twice.
_SEEN_JOBS_MAX = 32


def standing_gcode(record: Any) -> Optional[str]:
    """The G-code a standing options record asks for, or None for an absent
    record, one of another version, or one with nothing to send."""
    if not isinstance(record, dict):
        return None
    if record.get("version") != STANDING_OPTIONS_VERSION:
        return None
    gcode = record.get("gcode")
    if not isinstance(gcode, str):
        return None
    gcode = gcode.strip()
    return gcode or None


def hook_gcode(
    initiated_kind: Optional[str], screen_started: bool, enabled: bool, record: Any
) -> Optional[str]:
    """What to send at the start of a job, or None.

    A job a HelixScreen path started already carries its options: a resumed
    job had them applied when its part first started, and the screen's own
    starts send them before the print begins.
    """
    if initiated_kind is not None or screen_started or not enabled:
        return None
    return standing_gcode(record)


def job_still_running(status: Any, filename: str) -> bool:
    """Whether Klipper's print_stats says the job for `filename` is printing:
    the last thing checked before any standing gcode is sent."""
    stats = status.get("print_stats") if isinstance(status, dict) else None
    if not isinstance(stats, dict):
        return False
    return stats.get("state") == "printing" and stats.get("filename") == filename


class PrintHooks:
    """The Moonraker-facing half, created by helix_print.py."""

    def __init__(self, helix: Any, markers: Any, namespace: str) -> None:
        self.helix = helix
        self.server = helix.server
        # The app's settings namespace, where the record lives
        self.namespace = namespace
        # Starts the screen announced, matched to history's job by filename
        self.screen_starts = markers
        self._seen: List[Tuple[str, Any]] = []

    def start(self) -> None:
        self.server.register_endpoint(
            "/server/helix/print_hooks/screen_start", ["POST"], self.handle_screen_start
        )
        self.server.register_event_handler(
            "history:history_changed", self.on_history_changed
        )

    async def handle_screen_start(self, web_request: Any) -> Dict[str, Any]:
        """The screen is about to start `filename` and sends its own options
        with it. Called immediately before its start command; with `withdraw`
        after that command failed, so a start elsewhere gets its hooks."""
        filename = web_request.get_str("filename")
        if not filename:
            raise self.server.error("filename is required", 400)
        if web_request.get_boolean("withdraw", False):
            return {"filename": filename, "withdrawn": self.screen_starts.unmark(filename)}
        self.screen_starts.mark(filename, SCREEN_START_KIND)
        return {"filename": filename}

    def _first_time(self, job_id: Any, start_time: Any) -> bool:
        key = (str(job_id), start_time)
        if key in self._seen:
            return False
        self._seen.append(key)
        del self._seen[:-_SEEN_JOBS_MAX]
        return True

    async def _read_record(self) -> Any:
        database = self.helix.database
        if database is None:
            return None
        try:
            return await database.get_item(self.namespace, STANDING_OPTIONS_KEY, None)
        except Exception:
            return None

    async def _still_running(self, filename: str) -> bool:
        try:
            status = await self.helix.klippy_apis.query_objects(
                {"print_stats": ["state", "filename"]})
        except Exception as e:
            logging.info("HelixPrint: could not read print_stats: %s", e)
            return False
        return job_still_running(status, filename)

    async def on_history_changed(self, payload: Dict[str, Any]) -> None:
        if not isinstance(payload, dict) or payload.get("action") != "added":
            return
        job = payload.get("job")
        if not isinstance(job, dict) or not job.get("job_id"):
            return
        job_id = job["job_id"]
        filename = job.get("filename", "")
        start_time = job.get("start_time")
        # Everything read from in-memory state comes before the first await:
        # helix_print binds its own marker for this job before it awaits.
        screen_started = self.screen_starts.bind(filename, job_id, start_time) is not None
        if screen_started:
            self.screen_starts.forget(job_id)
        kind = self.helix.helix_initiated(job_id, filename, start_time)
        if not self._first_time(job_id, start_time):
            return
        enabled = await self.helix.feature_enabled(PRINT_HOOKS_FEATURE)
        gcode = hook_gcode(kind, screen_started, enabled, await self._read_record())
        if gcode is None:
            logging.debug("HelixPrint: no print hooks for job %s (%s)", job_id,
                          kind or (SCREEN_START_KIND if screen_started else "not opted in"))
            return
        if not await self._still_running(filename):
            logging.info("HelixPrint: job %s (%s) is no longer printing, no print hooks",
                         job_id, filename)
            return
        logging.info("HelixPrint: print hooks for job %s (%s):\n%s", job_id, filename, gcode)
        try:
            await self.helix.klippy_apis.run_gcode(gcode)
        except Exception as e:
            logging.warning("HelixPrint: print hooks for job %s failed: %s", job_id, e)
