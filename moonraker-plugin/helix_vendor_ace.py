# SPDX-License-Identifier: GPL-3.0-or-later
"""
ACE vendor push: the `/server/ace/*` REST bridge's state, pushed when it
changes.

An ACE driver whose Klipper object carries no slots (BunnyACE, DuckACE, the
ACEPRO manager-only shape) is read through a Moonraker bridge component,
`ace_status`, and the app's ACE backend polls its /status and /slots every
500 ms. This module asks the same bridge in-process, through the endpoints
it registered, and broadcasts the answers as the vendor `ace` payload,
{"status": <the /status result>, "slots": <the /slots result>}, only when
they changed. The app handles each exactly as the poll's answer, an `error`
answer included; a failed read is sent as {"error": ...} with the run of
failed reads in "failures", so each one reaches the app as a poll's would.

A printer whose ACE object carries slots is read by subscription and has
nothing to push, so the feature reports itself unavailable there, and no
read is made until Klipper's objects say which kind of printer this is.

App side: docs/devel/HELIXPRINT_PLUGIN.md, Vendor State Push. Python 3.8,
stdlib only.
"""

from __future__ import annotations

import logging
import sys
from typing import Any, Awaitable, Callable, Dict, Optional, Tuple

from helix_vendor_push import ReadHealth, VendorPush, Watcher, websocket_clients

ACE_VENDOR = "ace"
ACE_FEATURE = "vendor_ace"
ACE_FEATURE_VERSION = 1

ACE_STATUS_ENDPOINT = "/server/ace/status"
ACE_SLOTS_ENDPOINT = "/server/ace/slots"
# The app pins the bridge to instance 0: asked for none, the ACEPRO bridge
# serves the instance named by the seated tool, so the unit would change.
ACE_BRIDGE_ARGS = {"instance": "0"}

# The app's poll cadence
POLL_INTERVAL_S = 0.5

# The Klipper objects the app reads by subscription when they carry slots
ACE_OBJECTS = ("filament_hub", "ace", "ace_instance_0")

NO_BRIDGE_REASON = "no_ace_bridge"
OBJECTS_REASON = "ace_objects_carry_slots"
UNREADABLE_REASON = "bridge_unreadable"

BridgeCall = Callable[[Dict[str, Any]], Awaitable[Any]]


def bridge_answered(result: Any) -> bool:
    """Whether a bridge answer carries data: an object without `error`. The
    ACEPRO bridge answers an instance it does not have with only `error`."""
    return isinstance(result, dict) and "error" not in result


def objects_carry_slots(status: Any) -> bool:
    """Whether any ACE Klipper object in a query result carries a non-empty
    `slots` list: the app then reads them by subscription."""
    if not isinstance(status, dict):
        return False
    for name in ACE_OBJECTS:
        obj = status.get(name)
        if isinstance(obj, dict) and isinstance(obj.get("slots"), list) and obj["slots"]:
            return True
    return False


def _as_answer(result: Any) -> Dict[str, Any]:
    """A bridge answer as the app's poll would see it: the object, or an
    `error` object for a read that returned none."""
    if isinstance(result, dict):
        return result
    return {"error": "unreadable"}


def read_succeeded(status: Any, slots: Any) -> bool:
    """Whether a read of both endpoints returned data: either one is enough,
    as either one is for the app's poll."""
    return bridge_answered(status) or bridge_answered(slots)


def ace_payload(status: Any, slots: Any, failures: int) -> Dict[str, Any]:
    """The payload for one read of both endpoints. `failures` is the run of
    failed reads up to this one, sent while it is not zero."""
    payload: Dict[str, Any] = {"status": _as_answer(status), "slots": _as_answer(slots)}
    if failures:
        payload["failures"] = failures
    return payload


class _LocalRequest:
    """The WebRequest surface a bridge handler reads, for a Moonraker whose
    endpoint definitions cannot build a request themselves."""

    def __init__(self, endpoint: str, args: Dict[str, Any]) -> None:
        self.endpoint = endpoint
        self.args = dict(args)

    def get_endpoint(self) -> str:
        return self.endpoint

    def get_action(self) -> str:
        return "GET"

    def get_args(self) -> Dict[str, Any]:
        return dict(self.args)

    def get(self, key: str, default: Any = None) -> Any:
        return self.args.get(key, default)

    def get_str(self, key: str, default: Any = None) -> Any:
        value = self.args.get(key, default)
        return value if value is None else str(value)

    def get_int(self, key: str, default: Any = None) -> Any:
        value = self.args.get(key, default)
        return value if value is None else int(value)

    def get_float(self, key: str, default: Any = None) -> Any:
        value = self.args.get(key, default)
        return value if value is None else float(value)

    def get_boolean(self, key: str, default: Any = None) -> Any:
        value = self.args.get(key, default)
        if isinstance(value, str):
            return value.lower() in ("1", "true", "yes")
        return value if value is None else bool(value)


def find_endpoint(server: Any, endpoint: str) -> Optional[BridgeCall]:
    """A call into the handler another component registered for `endpoint`,
    or None when nothing is registered there.

    Moonraker 0.9 and later keep endpoint definitions in a class-level cache
    on `moonraker.common.APIDefinition`, whose `request()` builds a real
    WebRequest; earlier releases keep them in the application's `api_cache`.
    """
    common = sys.modules.get("moonraker.common")
    api_definition = getattr(common, "APIDefinition", None)
    get_cache = getattr(api_definition, "get_cache", None)
    definition = None
    if callable(get_cache):
        try:
            definition = get_cache().get(endpoint)
        except Exception:
            definition = None
    if definition is None:
        app = server.lookup_component("application", None)
        cache = getattr(app, "api_cache", None)
        if isinstance(cache, dict):
            definition = cache.get(endpoint)
    if definition is None:
        return None
    request_type = getattr(getattr(common, "RequestType", None), "GET", None)
    request = getattr(definition, "request", None)
    if callable(request) and request_type is not None:
        return lambda args: request(args, request_type)
    callback = getattr(definition, "callback", None)
    if not callable(callback):
        return None
    return lambda args: callback(_LocalRequest(endpoint, args))


class HelixVendorAce:
    """Pushes the ACE bridge's state on change. Created and started by
    helix_print.py."""

    def __init__(self, helix: Any) -> None:
        self.helix = helix
        self.server = helix.server
        self.push = VendorPush(self.server, ACE_VENDOR)
        self.watcher = Watcher("vendor_ace", POLL_INTERVAL_S, self.check)
        # Unknown (None) until Klipper is ready; nothing is read until then
        self.objects_serve: Optional[bool] = None
        self.health = ReadHealth()

    def _bridge(self) -> Tuple[Optional[BridgeCall], Optional[BridgeCall]]:
        return (find_endpoint(self.server, ACE_STATUS_ENDPOINT),
                find_endpoint(self.server, ACE_SLOTS_ENDPOINT))

    def availability(self) -> Tuple[bool, Optional[str]]:
        status, slots = self._bridge()
        if status is None and slots is None:
            return False, NO_BRIDGE_REASON
        if self.objects_serve:
            return False, OBJECTS_REASON
        if not self.health.ok():
            return False, UNREADABLE_REASON
        return True, None

    def start(self) -> None:
        self.server.register_event_handler("server:klippy_ready", self.on_klippy_ready)
        self.helix.eventloop.register_callback(self.watcher.run)

    def close(self) -> None:
        self.watcher.close()

    async def on_klippy_ready(self, *args: Any) -> None:
        """Whether the app reads the ACE objects instead, decided again on
        every Klipper start: a restart can bring another driver."""
        apis = self.helix.klippy_apis
        if apis is None:
            return
        try:
            status = await apis.query_objects({name: None for name in ACE_OBJECTS})
        except Exception as e:
            logging.debug("HelixPrint: ACE object query failed: %s", e)
            return
        self.objects_serve = objects_carry_slots(status)

    async def _ask(self, call: Optional[BridgeCall]) -> Any:
        if call is None:
            return None
        try:
            return await call(dict(ACE_BRIDGE_ARGS))
        except Exception as e:
            logging.debug("HelixPrint: ACE bridge read failed: %s", e)
            return None

    async def check(self) -> None:
        """Read the bridge and push what changed. Skipped until Klipper's
        objects show the bridge is the source, and while no client is
        connected to hear it."""
        if self.objects_serve is not False or websocket_clients(self.server) == 0:
            return
        status_call, slots_call = self._bridge()
        if status_call is None and slots_call is None:
            return
        status = await self._ask(status_call)
        slots = await self._ask(slots_call)
        self.health.note(read_succeeded(status, slots))
        self.push.emit_if_changed(ace_payload(status, slots, self.health.failures))
