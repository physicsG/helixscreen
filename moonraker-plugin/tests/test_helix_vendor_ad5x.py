# SPDX-License-Identifier: GPL-3.0-or-later
"""
Vendor push (helix_vendor_push.py) and the AD5X IFS module
(helix_vendor_ad5x.py): Adventurer5M.json broadcast once per change, the
feature unavailable where the file is not, and no switch.

Run with: pytest tests/test_helix_vendor_ad5x.py -v
"""

import os
import sys
from pathlib import Path

import pytest
import pytest_asyncio

sys.path.insert(0, str(Path(__file__).parent.parent))
sys.path.insert(0, str(Path(__file__).parent))

import helix_print
from helix_print import HELIX_SCREEN_NAMESPACE, PLUGIN_FEATURES_KEY
from helix_vendor_ad5x import (
    AD5X_FEATURE,
    AD5X_VENDOR,
    ADVENTURER_JSON,
    NO_FILE_REASON,
    UNREADABLE_REASON,
    names_adventurer_json,
)
from helix_vendor_push import (
    VENDOR_STATE_EVENT,
    VendorPush,
    Watcher,
    payload_key,
    register_vendor_notification,
)
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

SAMPLE = '{"FFMInfo":{"ffmColor1":"#FF0000","ffmType1":"PLA"}}'
EDITED = '{"FFMInfo":{"ffmColor1":"#00FF00","ffmType1":"PETG"}}'


class ConfigFileManager(MockFileManager):
    def __init__(self, gcodes_path, config_path):
        super().__init__(gcodes_path)
        self._config_path = config_path

    def get_directory(self, name):
        if name == "config":
            return self._config_path
        return super().get_directory(name)


class Server:
    """Records broadcasts and notification registrations."""

    def __init__(self, mock_server):
        self.sent = []
        self.registered = []
        mock_server.send_event = lambda event, *args: self.sent.append((event, args))
        mock_server.register_notification = self.registered.append


@pytest.fixture
def config_dir(tmp_path):
    path = tmp_path / "config"
    path.mkdir()
    return path


@pytest_asyncio.fixture
async def started(helix_print_component, mock_server, temp_gcodes_dir, config_dir):
    """The component started on a printer whose config root is config_dir."""
    mock_server.components["file_manager"] = ConfigFileManager(temp_gcodes_dir, str(config_dir))
    server = Server(mock_server)

    async def start():
        await helix_print_component.component_init()
        vendor = next(v for v in helix_print_component.vendor_pushes
                      if v.push.vendor == AD5X_VENDOR)
        return vendor

    return start, server


def write(config_dir, text, bump=0):
    path = config_dir / ADVENTURER_JSON
    path.write_text(text)
    if bump:
        st = path.stat()
        os.utime(path, ns=(st.st_atime_ns, st.st_mtime_ns + bump))
    return path


def pushes(server):
    return [args[0] for event, args in server.sent if event == VENDOR_STATE_EVENT]


async def status(mock_server):
    return await mock_server.endpoints["/server/helix/status"](MockWebRequest({}))


# ============================================================================
# Shared push plumbing
# ============================================================================


class TestVendorPush:
    def test_payload_key_ignores_key_order(self):
        assert payload_key({"a": 1, "b": [1, 2]}) == payload_key({"b": [1, 2], "a": 1})
        assert payload_key({"a": 1}) != payload_key({"a": 2})

    def test_sends_each_distinct_payload_once(self, mock_server):
        server = Server(mock_server)
        push = VendorPush(mock_server, "v")

        assert push.emit_if_changed({"x": 1}) is True
        assert push.emit_if_changed({"x": 1}) is False
        assert push.emit_if_changed({"x": 2}) is True
        assert push.emit_if_changed({"x": 1}) is True

        assert pushes(server) == [
            {"vendor": "v", "payload": {"x": 1}},
            {"vendor": "v", "payload": {"x": 2}},
            {"vendor": "v", "payload": {"x": 1}},
        ]

    def test_the_broadcast_is_registered_once_per_server(self, mock_server):
        server = Server(mock_server)
        VendorPush(mock_server, "a")
        VendorPush(mock_server, "b")
        register_vendor_notification(mock_server)
        assert server.registered == [VENDOR_STATE_EVENT]

    @pytest.mark.asyncio
    async def test_a_failing_check_does_not_stop_the_watcher(self):
        calls = []

        async def check():
            calls.append(1)
            if len(calls) == 1:
                raise RuntimeError("boom")

        watcher = Watcher("t", 1.0, check)

        async def sleep(_interval):
            if len(calls) >= 3:
                watcher.close()

        watcher._sleep = sleep
        await watcher.run()
        assert len(calls) == 3


# ============================================================================
# AD5X IFS
# ============================================================================


class TestFilelistMatch:
    def test_matches_only_the_file_in_the_config_root(self):
        assert names_adventurer_json({"item": {"root": "config", "path": ADVENTURER_JSON}})
        assert not names_adventurer_json({"item": {"root": "gcodes", "path": ADVENTURER_JSON}})
        assert not names_adventurer_json({"item": {"root": "config", "path": "printer.cfg"}})
        assert not names_adventurer_json(None)


class TestAd5x:
    @pytest.mark.asyncio
    async def test_unavailable_with_a_reason_where_the_file_is_not(self, started, mock_server):
        start, server = started
        await start()

        entry = (await status(mock_server))["features"][AD5X_FEATURE]
        assert entry["available"] is False
        assert entry["enabled"] is False
        assert entry["reason"] == NO_FILE_REASON

    @pytest.mark.asyncio
    async def test_available_and_on_with_no_switch(self, started, mock_server, config_dir):
        write(config_dir, SAMPLE)
        start, server = started
        vendor = await start()
        await vendor.helix.database.insert_item(
            HELIX_SCREEN_NAMESPACE, PLUGIN_FEATURES_KEY, {AD5X_FEATURE: False})
        assert (await status(mock_server))["features"][AD5X_FEATURE]["reason"] == \
            UNREADABLE_REASON

        await vendor.check()
        entry = (await status(mock_server))["features"][AD5X_FEATURE]
        assert entry == {"version": 1, "available": True, "enabled": True, "reason": None}

    @pytest.mark.asyncio
    async def test_pushes_the_text_once_per_change(self, started, config_dir):
        write(config_dir, SAMPLE)
        start, server = started
        vendor = await start()

        await vendor.check()
        await vendor.check()
        assert pushes(server) == [
            {"vendor": AD5X_VENDOR, "payload": {"file": ADVENTURER_JSON, "content": SAMPLE}}
        ]

        # Rewritten with the same text: the stat moved, nothing to push
        write(config_dir, SAMPLE, bump=10**9)
        await vendor.check()
        assert len(pushes(server)) == 1

        write(config_dir, EDITED, bump=2 * 10**9)
        await vendor.check()
        assert pushes(server)[-1]["payload"]["content"] == EDITED
        assert len(pushes(server)) == 2

    @pytest.mark.asyncio
    async def test_text_that_is_not_json_is_not_pushed(self, started, mock_server, config_dir):
        write(config_dir, SAMPLE[:20])
        start, server = started
        vendor = await start()
        await vendor.check()
        assert pushes(server) == []
        assert (await status(mock_server))["features"][AD5X_FEATURE]["reason"] == \
            UNREADABLE_REASON

        # Same stat, now complete: read again, since the half-written read was dropped
        write(config_dir, SAMPLE)
        await vendor.check()
        assert pushes(server)[-1]["payload"]["content"] == SAMPLE
        assert (await status(mock_server))["features"][AD5X_FEATURE]["available"] is True

    @pytest.mark.asyncio
    async def test_unchanged_stat_skips_the_read(self, started, config_dir, monkeypatch):
        write(config_dir, SAMPLE)
        start, server = started
        vendor = await start()
        await vendor.check()

        opened = []
        real_open = open
        monkeypatch.setattr("builtins.open",
                            lambda *a, **k: opened.append(a[0]) or real_open(*a, **k))
        await vendor.check()
        assert opened == []

    @pytest.mark.asyncio
    async def test_a_file_change_event_checks_at_once(self, started, config_dir):
        start, server = started
        vendor = await start()
        write(config_dir, SAMPLE)

        await vendor.on_filelist_changed({"item": {"root": "gcodes", "path": ADVENTURER_JSON}})
        assert pushes(server) == []
        await vendor.on_filelist_changed({"action": "modify_file",
                                          "item": {"root": "config", "path": ADVENTURER_JSON}})
        assert len(pushes(server)) == 1

    @pytest.mark.asyncio
    async def test_the_file_appearing_later_makes_it_available(self, started, mock_server,
                                                               config_dir):
        start, server = started
        vendor = await start()
        await vendor.check()
        assert pushes(server) == []

        write(config_dir, SAMPLE)
        await vendor.check()
        assert len(pushes(server)) == 1
        assert (await status(mock_server))["features"][AD5X_FEATURE]["available"] is True

    @pytest.mark.asyncio
    async def test_a_missing_module_reports_why(self, helix_print_component, mock_server,
                                                monkeypatch):
        monkeypatch.setattr(helix_print, "helix_vendor_ad5x", None)
        monkeypatch.setitem(helix_print.SIBLING_LOAD_ERRORS, "helix_vendor_ad5x",
                            "load_failed: SyntaxError")
        await helix_print_component.component_init()

        entry = (await status(mock_server))["features"][AD5X_FEATURE]
        assert entry["available"] is False
        assert entry["reason"] == "load_failed: SyntaxError"
