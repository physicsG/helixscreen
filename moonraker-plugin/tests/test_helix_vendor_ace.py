# SPDX-License-Identifier: GPL-3.0-or-later
"""
The ACE vendor push (helix_vendor_ace.py): the REST bridge read in-process
through the endpoints it registered, on every Moonraker generation, and
broadcast once per change; unavailable where there is no bridge or the ACE
objects carry the slots themselves.

Run with: pytest tests/test_helix_vendor_ace.py -v
"""

import sys
import types
from pathlib import Path

import pytest
import pytest_asyncio

sys.path.insert(0, str(Path(__file__).parent.parent))
sys.path.insert(0, str(Path(__file__).parent))

from helix_vendor_ace import (
    ACE_FEATURE,
    ACE_SLOTS_ENDPOINT,
    ACE_STATUS_ENDPOINT,
    ACE_VENDOR,
    NO_BRIDGE_REASON,
    OBJECTS_REASON,
    UNREADABLE_REASON,
    ace_payload,
    bridge_answered,
    find_endpoint,
    objects_carry_slots,
)
from helix_vendor_push import MAX_READ_FAILURES, VENDOR_STATE_EVENT
from test_helix_print import (  # noqa: F401  (fixtures are used by name)
    MockConfigHelper,
    MockDatabase,
    MockFileManager,
    MockHistory,
    MockKlippyApis,
    MockWebRequest,
    helix_print_component,
    mock_server,
    temp_gcodes_dir,
)

STATUS = {"status": "ready", "model": "ACE", "slots": [{"index": 0, "status": "ready"}],
          "ace_manager": {"current_index": 0}}
SLOTS = {"slots": [{"index": 0, "status": "ready", "color": [255, 0, 0]}]}


class Bridge:
    """An ace_status component as Moonraker sees it: two GET handlers that
    record the arguments they were asked with."""

    def __init__(self):
        self.status = dict(STATUS)
        self.slots = dict(SLOTS)
        self.asked = []

    async def handle_status(self, request):
        self.asked.append(("status", request.get_str("instance", None)))
        return self.status

    async def handle_slots(self, request):
        self.asked.append(("slots", request.get_str("instance", None)))
        return self.slots


class Definition:
    """A pre-0.9 APIDefinition: a callback only."""

    def __init__(self, callback):
        self.callback = callback


class Application:
    def __init__(self, bridge):
        self.api_cache = {
            ACE_STATUS_ENDPOINT: Definition(bridge.handle_status),
            ACE_SLOTS_ENDPOINT: Definition(bridge.handle_slots),
        }


class Websockets:
    def __init__(self, count):
        self.count = count

    def get_count(self):
        return self.count


@pytest_asyncio.fixture
async def ace(helix_print_component, mock_server):
    """The component started with an ACE bridge registered."""
    sent = []
    mock_server.send_event = lambda event, *args: sent.append((event, args))
    bridge = Bridge()
    mock_server.components["application"] = Application(bridge)
    await helix_print_component.component_init()
    vendor = next(v for v in helix_print_component.vendor_pushes if v.push.vendor == ACE_VENDOR)
    vendor.sent = sent
    vendor.bridge = bridge
    # Klipper ready, with ACE objects that carry no slots: the bridge is the source
    await vendor.on_klippy_ready()
    return vendor


def pushes(vendor):
    return [args[0] for event, args in vendor.sent if event == VENDOR_STATE_EVENT]


async def feature(mock_server):
    reply = await mock_server.endpoints["/server/helix/status"](MockWebRequest({}))
    return reply["features"][ACE_FEATURE]


class TestDecisions:
    def test_bridge_answered_rejects_an_error_answer(self):
        assert bridge_answered({"slots": []})
        assert not bridge_answered({"error": "Requested ACE instance 0 is unavailable"})
        assert not bridge_answered(None)
        assert not bridge_answered([])

    def test_objects_carry_slots_only_with_a_non_empty_list(self):
        assert objects_carry_slots({"ace": {"slots": [{"index": 0}]}})
        assert objects_carry_slots({"ace_instance_0": {"slots": [{}]}})
        assert objects_carry_slots({"filament_hub": {"slots": [{}]}})
        assert not objects_carry_slots({"ace": {"current_index": 0, "ace_instances": 1}})
        assert not objects_carry_slots({"ace": {"slots": []}})
        assert not objects_carry_slots({"other": {"slots": [{}]}})
        assert not objects_carry_slots(None)

    def test_payload_carries_every_answer_and_the_run_of_failures(self):
        assert ace_payload(STATUS, SLOTS, 0) == {"status": STATUS, "slots": SLOTS}
        assert ace_payload({"error": "x"}, SLOTS, 0) == {"status": {"error": "x"},
                                                         "slots": SLOTS}
        assert ace_payload(None, None, 2) == {"status": {"error": "unreadable"},
                                              "slots": {"error": "unreadable"},
                                              "failures": 2}


class TestEndpointLookup:
    @pytest.mark.asyncio
    async def test_reads_an_application_api_cache(self, mock_server):
        bridge = Bridge()
        mock_server.components["application"] = Application(bridge)

        call = find_endpoint(mock_server, ACE_STATUS_ENDPOINT)
        assert await call({"instance": "0"}) == STATUS
        assert bridge.asked == [("status", "0")]
        assert find_endpoint(mock_server, "/server/nothing") is None

    @pytest.mark.asyncio
    async def test_prefers_the_definition_cache_and_its_own_request(self, mock_server,
                                                                    monkeypatch):
        requested = []

        class ApiDef:
            def request(self, args, request_type):
                requested.append((args, request_type))

                async def answer():
                    return {"slots": []}
                return answer()

        class APIDefinition:
            @classmethod
            def get_cache(cls):
                return {ACE_SLOTS_ENDPOINT: ApiDef()}

        class RequestType:
            GET = "GET-type"

        common = types.ModuleType("moonraker.common")
        common.APIDefinition = APIDefinition
        common.RequestType = RequestType
        monkeypatch.setitem(sys.modules, "moonraker.common", common)

        call = find_endpoint(mock_server, ACE_SLOTS_ENDPOINT)
        assert await call({"instance": "0"}) == {"slots": []}
        assert requested == [({"instance": "0"}, "GET-type")]


class TestAce:
    @pytest.mark.asyncio
    async def test_unavailable_without_a_bridge(self, helix_print_component, mock_server):
        await helix_print_component.component_init()
        entry = await feature(mock_server)
        assert entry["available"] is False
        assert entry["enabled"] is False
        assert entry["reason"] == NO_BRIDGE_REASON

    @pytest.mark.asyncio
    async def test_ready_only_once_a_read_answered(self, ace, mock_server):
        entry = await feature(mock_server)
        assert entry["available"] is False
        assert entry["reason"] == UNREADABLE_REASON

        await ace.check()
        assert await feature(mock_server) == {
            "version": 1, "available": True, "enabled": True, "reason": None}

    @pytest.mark.asyncio
    async def test_failed_reads_make_it_unavailable_until_one_answers(self, ace, mock_server):
        await ace.check()
        ace.bridge.status = {"error": "unavailable"}
        ace.bridge.slots = {"error": "unavailable"}
        for _ in range(MAX_READ_FAILURES - 1):
            await ace.check()
        assert (await feature(mock_server))["available"] is True
        await ace.check()
        entry = await feature(mock_server)
        assert entry["available"] is False
        assert entry["reason"] == UNREADABLE_REASON

        ace.bridge.slots = dict(SLOTS)
        await ace.check()
        assert (await feature(mock_server))["available"] is True

    @pytest.mark.asyncio
    async def test_pushes_both_answers_once_per_change(self, ace):
        await ace.check()
        await ace.check()
        assert pushes(ace) == [
            {"vendor": ACE_VENDOR, "payload": {"status": STATUS, "slots": SLOTS}}
        ]
        assert ("status", "0") in ace.bridge.asked
        assert ("slots", "0") in ace.bridge.asked

        ace.bridge.slots = {"slots": [{"index": 0, "status": "empty"}]}
        await ace.check()
        assert len(pushes(ace)) == 2
        assert pushes(ace)[-1]["payload"]["slots"] == ace.bridge.slots

    @pytest.mark.asyncio
    async def test_each_failed_read_reaches_the_app(self, ace):
        ace.bridge.status = {"error": "unavailable"}
        ace.bridge.slots = {"error": "unavailable"}
        await ace.check()
        await ace.check()
        assert [p["payload"] for p in pushes(ace)] == [
            {"status": {"error": "unavailable"}, "slots": {"error": "unavailable"},
             "failures": 1},
            {"status": {"error": "unavailable"}, "slots": {"error": "unavailable"},
             "failures": 2},
        ]

    @pytest.mark.asyncio
    async def test_a_raising_bridge_pushes_nothing(self, ace):
        async def broken(request):
            raise RuntimeError("klippy gone")
        ace.bridge.handle_status = broken
        app = ace.server.lookup_component("application")
        app.api_cache[ACE_STATUS_ENDPOINT] = Definition(broken)
        app.api_cache[ACE_SLOTS_ENDPOINT] = Definition(broken)
        await ace.check()
        assert pushes(ace)[-1]["payload"] == {"status": {"error": "unreadable"},
                                              "slots": {"error": "unreadable"},
                                              "failures": 1}

    @pytest.mark.asyncio
    async def test_stands_down_where_the_objects_carry_slots(self, ace, mock_server):
        ace.helix.klippy_apis.query_objects.return_value = {
            "ace": {"slots": [{"index": 0}]}}
        await ace.on_klippy_ready()

        await ace.check()
        assert pushes(ace) == []
        entry = await feature(mock_server)
        assert entry["available"] is False
        assert entry["reason"] == OBJECTS_REASON

        ace.helix.klippy_apis.query_objects.return_value = {"ace": {"current_index": 0}}
        await ace.on_klippy_ready()
        await ace.check()
        assert len(pushes(ace)) == 1

    @pytest.mark.asyncio
    async def test_reads_nothing_before_klipper_says_which_printer(self, helix_print_component,
                                                                   mock_server):
        bridge = Bridge()
        mock_server.components["application"] = Application(bridge)
        await helix_print_component.component_init()
        vendor = next(v for v in helix_print_component.vendor_pushes
                      if v.push.vendor == ACE_VENDOR)
        await vendor.check()
        assert bridge.asked == []

    @pytest.mark.asyncio
    async def test_reads_nothing_while_no_client_is_connected(self, ace, mock_server):
        mock_server.components["websockets"] = Websockets(0)
        await ace.check()
        assert ace.bridge.asked == []

        mock_server.components["websockets"].count = 1
        await ace.check()
        assert len(pushes(ace)) == 1
