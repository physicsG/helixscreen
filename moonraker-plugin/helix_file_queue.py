# SPDX-License-Identifier: GPL-3.0-or-later
"""
File bookkeeping shared by the HelixPrint plugin's background features that
keep one record per file in the gcodes root: helix_analysis (a record per
G-code file) and helix_3mf (a record per .3mf source).

Each such feature
  - follows Moonraker's file_manager:filelist_changed events (FileTracker,
    file_event),
  - works through files one at a time in a queue (WorkQueue),
  - persists its records by gcodes-relative path in its own database
    namespace (RecordStore, namespace_items),
  - and finds every file at startup (scan_files).

The features that take over a slicer's "upload and print" (helix_3mf,
helix_fitcheck) share the upload-hook shape test, the reading of an upload
form, storing it without its print flag, and the start that stands in for
Moonraker's own (upload_hook_shape_ok, upload_print_request,
store_without_print, start_or_queue).

No Moonraker object is imported. Python 3.8, stdlib only.
"""

from __future__ import annotations

import asyncio
import inspect
import logging
import os
from pathlib import Path
from typing import (
    Any, Awaitable, Callable, Dict, List, NamedTuple, Optional, Set, Tuple, Union,
)

# job_state values after which a print no longer holds the printer
IDLE_JOB_STATES = ("standby", "complete", "cancelled", "error")


def is_hidden(path: str) -> bool:
    """Under a dot-directory or a dot-file: Moonraker's and our own staging."""
    return any(part.startswith(".") for part in path.split("/") if part)


# ============================================================================
# filelist_changed
# ============================================================================

class FileEvent(NamedTuple):
    """One change in the gcodes root. `action` is one of changed, deleted,
    moved, dir_deleted, dir_moved; `old` is the previous path of a move."""

    action: str
    path: str
    old: str


def _rel(item: Any) -> str:
    return ((item or {}).get("path") or "").strip("/")


def file_event(payload: Any) -> Optional[FileEvent]:
    """A filelist_changed payload as one FileEvent, or None for another root,
    an action no tracker follows, or one on hidden paths only.

    A hidden path is staging nobody tracks: a file moved out of one is a new
    file, and one moved into one is gone.
    """
    if not isinstance(payload, dict):
        return None
    item = payload.get("item") or {}
    if item.get("root", "gcodes") != "gcodes":
        return None
    action = payload.get("action", "")
    path = _rel(item)
    old = _rel(payload.get("source_item"))
    if not path:
        return None
    if action in ("create_file", "modify_file", "delete_file", "delete_dir"):
        if is_hidden(path):
            return None
        kind = {"create_file": "changed", "modify_file": "changed",
                "delete_file": "deleted", "delete_dir": "dir_deleted"}[action]
        return FileEvent(kind, path, "")
    if action in ("move_file", "move_dir"):
        old_hidden, new_hidden = not old or is_hidden(old), is_hidden(path)
        is_dir = action == "move_dir"
        if old_hidden and new_hidden:
            return None
        if old_hidden:
            # A directory arriving from staging brings no per-file events; the
            # next backfill finds its files.
            return None if is_dir else FileEvent("changed", path, "")
        if new_hidden:
            return FileEvent("dir_deleted" if is_dir else "deleted", old, "")
        return FileEvent("dir_moved" if is_dir else "moved", path, old)
    return None


def paths_under(paths: Any, directory: str) -> List[str]:
    """The paths in `paths` inside `directory`, at any depth."""
    prefix = directory.rstrip("/") + "/"
    return [p for p in paths if p.startswith(prefix)]


class FileTracker:
    """The filelist_changed half of a feature that keeps `records` keyed by
    gcodes-relative path. A subclass answers file_changed, file_deleted and
    file_moved; a directory event reaches it once per record inside it."""

    records: Dict[str, Any]

    def tracking(self) -> bool:
        """Whether events are followed now."""
        return True

    async def file_changed(self, path: str) -> None:
        raise NotImplementedError

    async def file_deleted(self, path: str) -> None:
        raise NotImplementedError

    async def file_moved(self, old: str, new: str) -> None:
        raise NotImplementedError

    async def on_filelist_changed(self, payload: Dict[str, Any], *args: Any) -> None:
        if not self.tracking():
            return
        event = file_event(payload)
        if event is None:
            return
        if event.action == "changed":
            await self.file_changed(event.path)
        elif event.action == "deleted":
            await self.file_deleted(event.path)
        elif event.action == "moved":
            await self.file_moved(event.old, event.path)
        elif event.action == "dir_deleted":
            for name in paths_under(list(self.records), event.path):
                await self.file_deleted(name)
        elif event.action == "dir_moved":
            prefix = event.old.rstrip("/") + "/"
            for name in paths_under(list(self.records), event.old):
                await self.file_moved(name, event.path + "/" + name[len(prefix):])


# ============================================================================
# Queue
# ============================================================================

class WorkQueue:
    """Keys processed one at a time, in order, each once however often it is
    put. `process(key, item)` handles one; an exception it raises is logged
    and the queue goes on. `ready()`, when given, is awaited before each key:
    False drops everything still queued."""

    def __init__(
        self,
        process: Callable[[str, Any], Awaitable[None]],
        ready: Optional[Callable[[], Awaitable[bool]]] = None,
        name: str = "queue",
    ) -> None:
        self._process = process
        self._ready = ready
        self._name = name
        self._items: Dict[str, Any] = {}
        self._task: Optional[asyncio.Future] = None

    def __contains__(self, key: object) -> bool:
        return key in self._items

    def __len__(self) -> int:
        return len(self._items)

    def put(self, key: str, make: Callable[[], Any] = lambda: None,
            front: bool = False) -> Any:
        """Queue `key`, with `make()` as its item unless it is queued already;
        the item. With `front` it goes ahead of everything waiting."""
        if key in self._items:
            item = self._items[key]
        else:
            item = self._items[key] = make()
        if front:
            self._items = {key: item, **{k: v for k, v in self._items.items() if k != key}}
        if self._task is None or self._task.done():
            self._task = asyncio.ensure_future(self._run())
        return item

    def discard(self, key: str) -> None:
        self._items.pop(key, None)

    def clear(self) -> None:
        self._items.clear()

    def cancel(self) -> None:
        if self._task is not None:
            self._task.cancel()

    async def wait_idle(self) -> None:
        while self._task is not None and not self._task.done():
            try:
                await self._task
            except asyncio.CancelledError:
                if not self._task.cancelled():
                    raise

    async def _run(self) -> None:
        while self._items:
            try:
                if self._ready is not None and not await self._ready():
                    self._items.clear()
                    return
            except Exception:
                logging.exception("HelixPrint: %s stopped", self._name)
                return
            if not self._items:
                return
            key = next(iter(self._items))
            item = self._items.pop(key)
            try:
                await self._process(key, item)
            except Exception:
                logging.exception("HelixPrint: %s failed on %s", self._name, key)


# ============================================================================
# Records
# ============================================================================

async def namespace_items(database: Any, namespace: str) -> List[Tuple[str, Any]]:
    """(key, value) pairs of a namespace; empty when it does not exist.

    get_item with no key returns the whole namespace as a dict, and with a
    default it does not raise for a namespace never written. That is what
    Moonraker 0.9.3's database offers; ns_items is used where get_item
    cannot answer that way.
    """
    try:
        whole = await database.get_item(namespace, None, {})
    except Exception:
        whole = None
    if isinstance(whole, dict):
        return list(whole.items())
    if hasattr(database, "ns_items"):
        try:
            return list(await database.ns_items(namespace))
        except Exception:
            return []
    return []


DatabaseRef = Callable[[], Any]


class RecordStore:
    """One database namespace of records keyed by gcodes-relative path.
    `database()` answers Moonraker's database component, or None where there
    is none; every call is best effort and logs what fails."""

    def __init__(self, database: DatabaseRef, namespace: str, what: str) -> None:
        self._database = database
        self.namespace = namespace
        self.what = what

    async def load(self) -> Dict[str, Any]:
        db = self._database()
        if db is None:
            return {}
        return dict(await namespace_items(db, self.namespace))

    async def save(self, key: str, value: Any) -> None:
        db = self._database()
        if db is None:
            return
        try:
            # A one-element list names the key whole, dots included.
            await db.insert_item(self.namespace, [key], value)
        except Exception as e:
            logging.warning("HelixPrint: could not save %s of %s: %s", self.what, key, e)

    async def delete(self, key: str) -> None:
        db = self._database()
        if db is None:
            return
        try:
            await db.delete_item(self.namespace, [key])
        except Exception:
            pass


# ============================================================================
# Scan
# ============================================================================

def scan_files(
    root: Union[str, Path], keep: Callable[[str], bool]
) -> Dict[str, Tuple[int, float]]:
    """{gcodes-relative path: (size, mtime)} of every visible file under
    `root` that `keep(path)` accepts. Blocking: run it off the event loop."""
    found: Dict[str, Tuple[int, float]] = {}
    root = str(root)
    for dirpath, dirs, files in os.walk(root):
        dirs[:] = [d for d in dirs if not d.startswith(".")]
        for name in files:
            if name.startswith("."):
                continue
            full = os.path.join(dirpath, name)
            rel = os.path.relpath(full, root).replace(os.sep, "/")
            if not keep(rel):
                continue
            try:
                st = os.stat(full)
            except OSError:
                continue
            found[rel] = (st.st_size, st.st_mtime)
    return found


# ============================================================================
# Upload and print
# ============================================================================

def upload_hook_shape_ok(method: Any) -> bool:
    """Whether finalize_upload looks like the `async (form_args) -> dict` we wrap."""
    if method is None or not inspect.iscoroutinefunction(method):
        return False
    try:
        params = list(inspect.signature(method).parameters.values())
    except (TypeError, ValueError):
        return False
    return len(params) == 1 and params[0].kind in (
        inspect.Parameter.POSITIONAL_ONLY,
        inspect.Parameter.POSITIONAL_OR_KEYWORD,
    )


# The longest a slicer's upload-and-print request is held open, counted from
# when the hook takes it: nginx in front of Moonraker (the KIAUH Mainsail and
# Fluidd configs) answers 504 after 60 s with no proxy_read_timeout set, and a
# hold that ends first means the slicer never reports a failure for an upload
# whose print then starts.
UPLOAD_HOLD_S = 45.0

# Paths whose start or queue call is under way in this process. Checked and
# claimed with no await between, so two holds of one path start it once.
_starts_in_flight = set()  # type: Set[str]


def print_result(started: bool = False, queued: bool = False) -> Dict[str, bool]:
    """What a hook did about a requested print, in the shape of Moonraker's
    upload response."""
    return {"print_started": started, "print_queued": queued}


class UploadRequest(NamedTuple):
    """What an upload form asks: its root, file name (lower-cased) and whether
    it asks for a print (OctoPrint-style `print=true`)."""

    root: str
    filename: str
    wants_print: bool


def upload_print_request(form_args: Any) -> Optional[UploadRequest]:
    """The upload form @p form_args as an UploadRequest; None for a form that
    is not a dict."""
    if not isinstance(form_args, dict):
        return None
    return UploadRequest(
        root=str(form_args.get("root", "gcodes")).lower(),
        filename=str(form_args.get("filename", "")).lower(),
        wants_print=str(form_args.get("print", "false")) == "true",
    )


async def store_without_print(finalize: Callable[[Dict[str, Any]], Awaitable[Any]],
                              form_args: Dict[str, Any]) -> Tuple[Any, str]:
    """Moonraker's upload of @p form_args with its print flag removed: its
    response, and the gcodes-relative path it stored the file at ("" when it
    names none)."""
    stored_args = dict(form_args)
    stored_args.pop("print", None)
    result = await finalize(stored_args)
    item = (result or {}).get("item") or {}
    return result, str(item.get("path", "")).lstrip("/")


async def already_started(server: Any, klippy_apis: Any, path: str) -> bool:
    """Whether @p path is the file Klipper is printing now or is waiting in
    Moonraker's job queue: starting or queueing it again would print it twice.
    Klipper keeps print_stats.filename after a print ends, so only a printing
    or paused state counts. A question that cannot be answered reads as no."""
    try:
        status = await klippy_apis.query_objects({"print_stats": None})
        stats = (status or {}).get("print_stats") or {}
        if stats.get("filename") == path and stats.get("state") in ("printing", "paused"):
            return True
    except Exception:
        pass
    lookup = getattr(server, "lookup_component", None)
    job_queue = lookup("job_queue", None) if lookup is not None else None
    jobs = getattr(job_queue, "queued_jobs", None)
    if isinstance(jobs, dict):
        return any(getattr(job, "filename", None) == path for job in jobs.values())
    return False


async def start_or_queue(server: Any, klippy_apis: Any, file_manager: Any,
                         path: str, user: Any) -> Dict[str, bool]:
    """Start @p path as Moonraker's own upload path would: start it, and when
    that is refused (a print running) queue it if the file manager queues
    uploads. Nothing when it already prints or waits in the queue, or when a
    start of it is already under way here. Returns print_result()."""
    if path in _starts_in_flight:
        logging.info(f"HelixPrint: not starting {path}: a start of it is under way")
        return print_result()
    _starts_in_flight.add(path)
    try:
        return await _start_or_queue(server, klippy_apis, file_manager, path, user)
    finally:
        _starts_in_flight.discard(path)


async def _start_or_queue(server: Any, klippy_apis: Any, file_manager: Any,
                          path: str, user: Any) -> Dict[str, bool]:
    if await already_started(server, klippy_apis, path):
        logging.info(f"HelixPrint: not starting {path}: it is already printing or queued")
        return print_result()
    try:
        await klippy_apis.start_print(path, user=user)
        return print_result(started=True)
    except Exception as e:
        logging.info(f"HelixPrint: not starting {path}: {e}")
    if not getattr(file_manager, "queue_gcodes", False):
        return print_result()
    job_queue = server.lookup_component("job_queue", None)
    if job_queue is None:
        return print_result()
    try:
        await job_queue.queue_job(path, check_exists=False, user=user)
    except Exception as e:
        logging.info(f"HelixPrint: not queueing {path}: {e}")
        return print_result()
    return print_result(queued=True)
