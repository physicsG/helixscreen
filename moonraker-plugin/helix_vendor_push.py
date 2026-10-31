# SPDX-License-Identifier: GPL-3.0-or-later
"""
Vendor push: what every vendor state module shares.

A vendor module watches state a filament backend in the app would otherwise
poll (a file, a REST bridge) and broadcasts it as
`notify_helix_vendor_state` with params [{"vendor": ..., "payload": ...}],
only when it changed. The payload is what the app's poll reads, unchanged,
so the app runs the same parse on a push as on a poll.

App side: docs/devel/HELIXPRINT_PLUGIN.md, Vendor State Push. One module per
vendor (helix_vendor_ad5x.py, helix_vendor_ace.py); this one holds no vendor
knowledge. Python 3.8, stdlib only.
"""

from __future__ import annotations

import asyncio
import json
import logging
from typing import Any, Awaitable, Callable, Dict, Optional

# Broadcast as notify_helix_vendor_state, params [{"vendor", "payload"}]
VENDOR_STATE_EVENT = "helix_print:helix_vendor_state"


def payload_key(payload: Any) -> str:
    """A canonical rendering of a payload, equal for equal payloads whatever
    the key order."""
    return json.dumps(payload, sort_keys=True, separators=(",", ":"), default=str)


def vendor_message(vendor: str, payload: Any) -> Dict[str, Any]:
    return {"vendor": vendor, "payload": payload}


# Set on a server once the broadcast is registered with it
_REGISTERED_ATTR = "_helix_vendor_state_registered"


def register_vendor_notification(server: Any) -> None:
    """Register the broadcast once per server: every vendor module shares it,
    and a second registration would send every message twice."""
    if getattr(server, _REGISTERED_ATTR, False):
        return
    register = getattr(server, "register_notification", None)
    if register is None:
        return
    register(VENDOR_STATE_EVENT)
    setattr(server, _REGISTERED_ATTR, True)


def websocket_clients(server: Any) -> Optional[int]:
    """How many websocket clients are connected, or None when this
    Moonraker cannot say."""
    manager = server.lookup_component("websockets", None)
    get_count = getattr(manager, "get_count", None)
    if get_count is None:
        return None
    try:
        return int(get_count())
    except Exception:
        return None


class VendorPush:
    """Broadcasts one vendor's state when it differs from the last one sent."""

    def __init__(self, server: Any, vendor: str) -> None:
        self.server = server
        self.vendor = vendor
        self._last_key: Optional[str] = None
        register_vendor_notification(server)

    def emit_if_changed(self, payload: Any) -> bool:
        """Send `payload` unless it equals the last one sent. True when sent."""
        key = payload_key(payload)
        if key == self._last_key:
            return False
        self._last_key = key
        send = getattr(self.server, "send_event", None)
        if send is not None:
            send(VENDOR_STATE_EVENT, vendor_message(self.vendor, payload))
        return True


# Consecutive failed reads after which a feature reports itself unavailable
MAX_READ_FAILURES = 3


class ReadHealth:
    """Whether a module's reads of its vendor state work. A feature is only
    worth the app's trust once a read has returned data, and stops being so
    after MAX_READ_FAILURES failed reads in a row: the app stops polling while
    the feature is ready, so a push that cannot read must not look ready."""

    def __init__(self) -> None:
        self.succeeded = False
        self.failures = 0

    def note(self, success: bool) -> None:
        if success:
            self.succeeded = True
            self.failures = 0
        else:
            self.failures += 1

    def ok(self) -> bool:
        return self.succeeded and self.failures < MAX_READ_FAILURES


class Watcher:
    """Runs `check` every `interval` seconds until closed. A check that raises
    is logged and the next one runs on time."""

    def __init__(self, name: str, interval: float, check: Callable[[], Awaitable[None]]) -> None:
        self.name = name
        self.interval = interval
        self.check = check
        self.closed = False
        self._sleep: Callable[[float], Awaitable[Any]] = asyncio.sleep

    async def run(self) -> None:
        while not self.closed:
            try:
                await self.check()
            except Exception:
                logging.exception(f"HelixPrint: {self.name} check failed")
            await self._sleep(self.interval)

    def close(self) -> None:
        self.closed = True
