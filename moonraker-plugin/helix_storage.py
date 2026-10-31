# SPDX-License-Identifier: GPL-3.0-or-later
"""
HelixPrint storage report and guard.

`server.helix.storage` reports what fills the printer's disks, by consumer:
the print copies and links (`HelixPrint.staging_dirs()`), .3mf conversion
outputs, file analysis sidecars, history photos, Moonraker timelapse renders,
logs, Moonraker's thumbnails, the user's own G-code, and power-loss recovery's
snapshot and mesh. `server.helix.storage_cleanup {consumer, older_than}`
removes one consumer's files older than `older_than` seconds; it refuses
unknown consumers and every consumer that holds the user's files or state
another feature needs (CONSUMERS, `cleanable`).

Cleanup never trusts a directory it was configured with: a staging directory
name must be a plain name, the timelapse and logs roots may not hold the
gcodes, config or database directories (root_problem), every walked directory
is resolved and must lie inside its root, and timelapse and logs remove only
files their name rule accepts (name_allowed). A file a print or a waiting
power-loss snapshot uses is never removed (HelixStorage.protected_paths), and
photos are removed through helix_photos, which owns their records.

`print_modified` / `print_edited` ask `free_space_refusal()` before they print
a copy, so a print never starts on a disk with less than FREE_SPACE_FLOOR_BYTES
left. Automatic cleanup by age (`storage_auto_cleanup`) acts, so it is off until
the user opts in.

Pure rules sit at the top and touch no Moonraker object; HelixStorage is the
Moonraker-facing half, created by helix_print.py.
"""

from __future__ import annotations

import logging
import os
import shutil
import time
from typing import Any, Dict, Iterable, List, NamedTuple, Optional, Set, Tuple

import helix_file_queue as file_queue
import helix_gcode_reader as reader
from helix_photos import PHOTOS_DIR

STORAGE_FEATURE = "storage"
STORAGE_FEATURE_VERSION = 1
AUTO_CLEANUP_FEATURE = "storage_auto_cleanup"
AUTO_CLEANUP_FEATURE_VERSION = 1

# The free space a print started through the plugin must leave on the gcodes
# volume: room for Klipper's and Moonraker's logs, Moonraker's database and a
# timelapse render while the print runs.
FREE_SPACE_FLOOR_BYTES = 100 * 1024 * 1024

# Power-loss recovery's snapshot and mesh, under Moonraker's data directory.
PLR_DIR = "helix-plr"

# Moonraker's thumbnail directories, beside the files they belong to.
THUMBS_DIR = ".thumbs"

# Moonraker's database directory, under its data directory.
DATABASE_DIR = "database"

ROOT_GCODES = "gcodes"
ROOT_TIMELAPSE = "timelapse"
ROOT_LOGS = "logs"
ROOT_DATA = "data"
ROOT_CONFIG = "config"

# Directories of other consumers under gcodes: a staging directory named like
# one would hand its files to the print-copy cleanup.
OTHER_CONSUMER_DIRS = (reader.LAYER_INDEX_DIR, PHOTOS_DIR, THUMBS_DIR)

# What moonraker-timelapse writes: renders and their preview frames.
TIMELAPSE_EXTENSIONS = (".mp4", ".mkv", ".webm", ".avi", ".mov", ".gif", ".jpg", ".png")

# Automatic cleanup runs this long after Moonraker starts, then this often;
# a run that finds a print going tries again this much later.
AUTO_CLEANUP_FIRST_DELAY_S = 600.0
AUTO_CLEANUP_INTERVAL_S = 24 * 3600.0
AUTO_CLEANUP_BUSY_RETRY_S = 3600.0

DAY_S = 24 * 3600.0


class Consumer(NamedTuple):
    name: str
    root: str
    # The consumer's top-level directories under its root; empty for one that
    # owns its whole root or is found by classify_gcodes_path(). `helix_temp`
    # takes HelixPrint.staging_dirs() instead.
    dirs: Tuple[str, ...]
    # Whether storage_cleanup may remove its files.
    cleanable: bool
    # A file younger than this is never removed, whatever `older_than` says:
    # it may still be on its way to a print, a render or a log rotation.
    min_age_s: float
    # Automatic cleanup removes its files older than this; None = never.
    auto_age_s: Optional[float]


# Report order.
CONSUMERS = (
    Consumer("helix_temp", ROOT_GCODES, (), True, 600.0, 7 * DAY_S),
    Consumer("3mf_outputs", ROOT_GCODES, (), False, 0.0, None),
    Consumer("analysis_sidecars", ROOT_GCODES, (reader.LAYER_INDEX_DIR,), True, 0.0, None),
    Consumer("photos", ROOT_GCODES, (PHOTOS_DIR,), True, 0.0, None),
    Consumer("timelapse", ROOT_TIMELAPSE, (), True, 300.0, None),
    Consumer("logs", ROOT_LOGS, (), True, 300.0, 30 * DAY_S),
    Consumer("thumbnails", ROOT_GCODES, (), False, 0.0, None),
    Consumer("user_gcode", ROOT_GCODES, (), False, 0.0, None),
    Consumer("plr", ROOT_DATA, (PLR_DIR,), False, 0.0, None),
)

CONSUMERS_BY_NAME = {c.name: c for c in CONSUMERS}


class StorageError(Exception):
    """A request the storage endpoints refuse; the message is user-facing."""


# =============================================================================
# Pure rules
# =============================================================================


def consumer_dirs(consumer: Consumer, staging_dirs: Tuple[str, ...]) -> Tuple[str, ...]:
    """A consumer's top-level directories under its root."""
    return staging_dirs if consumer.name == "helix_temp" else consumer.dirs


def classify_gcodes_path(rel: str, staging_dirs: Iterable[str], outputs: Set[str]) -> str:
    """The consumer a file under the gcodes root belongs to. `rel` is
    gcodes-relative with `/` separators; `outputs` holds the .3mf converter's
    own outputs. Everything no plugin feature owns is the user's."""
    parts = rel.split("/")
    top = parts[0]
    if len(parts) > 1:
        if top in staging_dirs:
            return "helix_temp"
        if top == reader.LAYER_INDEX_DIR:
            return "analysis_sidecars"
        if top == PHOTOS_DIR:
            return "photos"
    if THUMBS_DIR in parts[:-1]:
        return "thumbnails"
    if rel in outputs:
        return "3mf_outputs"
    return "user_gcode"


def is_rotated_log(name: str) -> bool:
    """A log a service rotated away (`klippy.log.2026-10-20`,
    `moonraker.log.1`), as opposed to one it is still writing (`klippy.log`)
    or any other file."""
    stem, sep, suffix = name.partition(".log.")
    return bool(stem) and bool(sep) and bool(suffix)


def name_allowed(consumer: Consumer, name: str) -> bool:
    """The positive name rule of a consumer whose root holds more than its
    files: timelapse removes only render outputs, logs only rotated logs,
    analysis only complete layer indexes (a `.tmp` is prune_layer_indexes'
    to judge)."""
    if consumer.name == "timelapse":
        return name.lower().endswith(TIMELAPSE_EXTENSIONS)
    if consumer.name == "logs":
        return is_rotated_log(name)
    if consumer.name == "analysis_sidecars":
        return name.endswith(reader.LAYER_INDEX_SUFFIX)
    return True


def valid_dir_name(name: Any) -> bool:
    """A staging directory name cleanup may walk: one plain path segment."""
    return (isinstance(name, str) and name == name.strip() and name not in ("", ".", "..")
            and "/" not in name and "\\" not in name)


def is_within(path: str, root: str) -> bool:
    """`path` lies strictly below `root`; both already resolved."""
    root = root.rstrip(os.sep) or os.sep
    return path != root and path.startswith(root if root == os.sep else root + os.sep)


def root_problem(roots: Dict[str, Optional[str]], staging_dirs: Tuple[str, ...]) -> Optional[str]:
    """Why cleanup must not run here, or None. `roots` maps ROOT_* (and
    ROOT_CONFIG) to a directory. A staging name that is not one plain segment
    would walk the gcodes root or above it; a timelapse or logs root that is,
    or holds, the gcodes, config, database or data directory would delete
    the user's files. The answer is a reason code the app translates, with
    the offending name or path as a ': detail' suffix."""
    for name in staging_dirs:
        if not valid_dir_name(name) or name in OTHER_CONSUMER_DIRS:
            return "unsafe_staging_dir: %s" % (name,)
    data = roots.get(ROOT_DATA)
    guarded = {
        "gcodes": roots.get(ROOT_GCODES),
        "config": roots.get(ROOT_CONFIG),
        "database": os.path.join(data, DATABASE_DIR) if data else None,
        "data": data,
    }
    for root_name in (ROOT_TIMELAPSE, ROOT_LOGS):
        root = roots.get(root_name)
        if not root:
            continue
        resolved = os.path.realpath(root)
        for what, path in guarded.items():
            if not path:
                continue
            other = os.path.realpath(path)
            if resolved == other or is_within(other, resolved):
                return "unsafe_root: %s" % (root,)
    return None


def cleanup_tops(consumer: Consumer, root: str, staging_dirs: Tuple[str, ...]) -> List[str]:
    """The directories a cleanup of `consumer` walks, each resolved. A
    directory under the gcodes or data root must lie strictly inside it;
    only timelapse and logs walk their whole root, which root_problem
    vetted."""
    resolved_root = os.path.realpath(root)
    dirs = consumer_dirs(consumer, staging_dirs)
    if not dirs:
        if consumer.root not in (ROOT_TIMELAPSE, ROOT_LOGS):
            raise StorageError("%s has no directory of its own" % consumer.name)
        return [resolved_root]
    tops = []
    for d in dirs:
        if not valid_dir_name(d):
            raise StorageError("unsafe directory %r" % (d,))
        if os.path.islink(os.path.join(root, d)):
            # The plugin makes these as real directories; a link may lead
            # into a user folder.
            raise StorageError("%s is a link" % d)
        top = os.path.realpath(os.path.join(root, d))
        if not is_within(top, resolved_root):
            raise StorageError("%s resolves outside its root" % d)
        tops.append(top)
    return tops


def free_space_refusal(free_bytes: Optional[int], incoming_bytes: int,
                       floor: int = FREE_SPACE_FLOOR_BYTES) -> Optional[str]:
    """Why a print must not start, or None. `free_bytes` is the volume's free
    space now (None when it cannot be read, which never refuses: a guard that
    cannot measure must not block printing); `incoming_bytes` what the plugin
    is about to write there first."""
    if free_bytes is None:
        return None
    left = free_bytes - max(0, incoming_bytes)
    if left >= floor:
        return None
    return (
        "Not enough free space on the printer: this print would leave %s free, "
        "below the %s HelixPrint keeps free. Delete old files, or clean up in "
        "Settings > System > Storage." % (format_mb(max(0, left)), format_mb(floor))
    )


def format_mb(n: int) -> str:
    return "%d MB" % (n // (1024 * 1024))


def cleanup_candidates(
    consumer: Consumer,
    files: Iterable[Tuple[str, int, float]],
    now: float,
    older_than: float,
    protected: Set[str],
) -> List[Tuple[str, int]]:
    """The files of `consumer` a cleanup removes, as (path, size): those its
    name rule accepts, at least `older_than` seconds old (and never younger
    than the consumer's `min_age_s`), and not protected. `files` holds
    (absolute path, size, mtime)."""
    if not consumer.cleanable:
        return []
    min_age = max(float(older_than), consumer.min_age_s)
    chosen = []  # type: List[Tuple[str, int]]
    for path, size, mtime in files:
        if os.path.abspath(path) in protected or os.path.realpath(path) in protected:
            continue
        if not name_allowed(consumer, os.path.basename(path)):
            continue
        if now - mtime < min_age:
            continue
        chosen.append((path, size))
    return chosen


def parse_older_than(value: Any) -> float:
    """`older_than` in seconds: a number of zero or more."""
    if isinstance(value, bool):
        raise StorageError("older_than must be a number of seconds")
    try:
        seconds = float(value)
    except (TypeError, ValueError):
        raise StorageError("older_than must be a number of seconds")
    if seconds != seconds or seconds < 0:
        raise StorageError("older_than must be zero or more seconds")
    return seconds


def resolve_consumer(name: Any) -> Consumer:
    """The consumer a cleanup names; refuses unknown names and every consumer
    that is not cleanable."""
    consumer = CONSUMERS_BY_NAME.get(name) if isinstance(name, str) else None
    if consumer is None:
        raise StorageError("Unknown storage consumer: %r" % (name,))
    if not consumer.cleanable:
        raise StorageError("%s cannot be cleaned up from here" % consumer.name)
    return consumer


# =============================================================================
# Filesystem walks (run off the event loop)
# =============================================================================


def _walk_files(top: str, skip: Set[str]) -> Iterable[Tuple[str, int, float]]:
    """(path, size, mtime) of every file under `top`, without following
    links into directories, and leaving out the directories in `skip`
    (another root mounted inside this one)."""
    for dirpath, dirs, files in os.walk(top, followlinks=False):
        dirs[:] = [d for d in dirs if os.path.realpath(os.path.join(dirpath, d)) not in skip]
        for name in files:
            path = os.path.join(dirpath, name)
            try:
                st = os.lstat(path)
            except OSError:
                continue
            yield path, st.st_size, st.st_mtime


def _empty_usage() -> Dict[str, Dict[str, Any]]:
    return {c.name: {"bytes": 0, "files": 0} for c in CONSUMERS}


def scan_usage(
    roots: Dict[str, Optional[str]],
    staging_dirs: Tuple[str, ...],
    outputs: Set[str],
) -> Dict[str, Dict[str, Any]]:
    """Bytes and file count per consumer. `roots` maps ROOT_* to a directory,
    None for one this Moonraker does not have (its consumers report
    `available: false`)."""
    usage = _empty_usage()
    present = {k: v for k, v in roots.items()
               if k in (ROOT_GCODES, ROOT_TIMELAPSE, ROOT_LOGS, ROOT_DATA)
               and v and os.path.isdir(v)}
    others = {os.path.realpath(v) for k, v in present.items()
              if k in (ROOT_TIMELAPSE, ROOT_LOGS)}
    gcodes = present.get(ROOT_GCODES)
    if gcodes is not None:
        for path, size, _mtime in _walk_files(gcodes, others):
            rel = os.path.relpath(path, gcodes).replace(os.sep, "/")
            entry = usage[classify_gcodes_path(rel, staging_dirs, outputs)]
            entry["bytes"] += size
            entry["files"] += 1
    for consumer in CONSUMERS:
        if consumer.root == ROOT_GCODES:
            continue
        root = present.get(consumer.root)
        if root is None:
            continue
        tops = [os.path.join(root, d) for d in consumer.dirs] or [root]
        for top in tops:
            for _path, size, _mtime in _walk_files(top, set()):
                usage[consumer.name]["bytes"] += size
                usage[consumer.name]["files"] += 1
    for consumer in CONSUMERS:
        root = present.get(consumer.root)
        usage[consumer.name]["available"] = root is not None
        usage[consumer.name]["cleanable"] = consumer.cleanable and root is not None
    return usage


def consumer_files(tops: List[str]) -> List[Tuple[str, int, float]]:
    """Every file under the resolved directories `tops`."""
    found = []  # type: List[Tuple[str, int, float]]
    for top in tops:
        if os.path.isdir(top):
            found.extend(_walk_files(top, set()))
    return found


def remove_files(paths: List[Tuple[str, int]], keep_dirs: Iterable[str]) -> Tuple[int, int, int]:
    """Remove `paths`, then the directories they leave empty, except
    `keep_dirs` and anything above them. (removed, bytes freed, errors)."""
    removed = freed = errors = 0
    parents = set()  # type: Set[str]
    for path, size in paths:
        try:
            os.remove(path)
        except FileNotFoundError:
            continue
        except OSError as e:
            logging.warning("HelixPrint: could not remove %s: %s", path, e)
            errors += 1
            continue
        removed += 1
        freed += size
        parents.add(os.path.dirname(path))
    keep = {os.path.abspath(d) for d in keep_dirs}
    for parent in sorted(parents, key=len, reverse=True):
        d = os.path.abspath(parent)
        while d not in keep and any(d.startswith(k + os.sep) for k in keep):
            try:
                os.rmdir(d)
            except OSError:
                break
            d = os.path.dirname(d)
    return removed, freed, errors


def disk_usage(path: Optional[str]) -> Optional[Dict[str, int]]:
    if not path:
        return None
    try:
        du = shutil.disk_usage(path)
    except OSError:
        return None
    return {"total": du.total, "used": du.used, "free": du.free}


# =============================================================================
# Moonraker-facing half
# =============================================================================


class HelixStorage:
    """The storage endpoints, the free-space guard and automatic cleanup inside
    a running Moonraker. helix_print.py creates one in its component_init."""

    def __init__(self, helix: Any) -> None:
        self.helix = helix
        self.server = helix.server
        self.eventloop = self.server.get_event_loop()
        self.clock = time.time
        self.last_auto_cleanup: Optional[Dict[str, Any]] = None
        self._timer: Any = None

    def start(self) -> None:
        self.server.register_endpoint(
            "/server/helix/storage", ["GET"], self.handle_storage)
        self.server.register_endpoint(
            "/server/helix/storage_cleanup", ["POST"], self.handle_storage_cleanup)
        self._schedule(AUTO_CLEANUP_FIRST_DELAY_S)

    def close(self) -> None:
        cancel = getattr(self._timer, "cancel", None)
        if cancel is not None:
            cancel()
        self._timer = None

    # -- roots ---------------------------------------------------------------

    def roots(self) -> Dict[str, Optional[str]]:
        helix = self.helix
        fm = helix.file_manager
        get = getattr(fm, "get_directory", None)

        def root(name: str) -> Optional[str]:
            try:
                path = get(name) if get is not None else ""
            except Exception:
                path = ""
            return path or None

        gcodes = str(helix.gc_path) if helix.gc_path is not None else None
        return {
            ROOT_GCODES: gcodes,
            ROOT_TIMELAPSE: root("timelapse"),
            ROOT_LOGS: root("logs"),
            ROOT_DATA: str(helix._data_dir()),
            ROOT_CONFIG: root("config"),
        }

    def staging_dirs(self) -> Tuple[str, ...]:
        return tuple(self.helix.staging_dirs())

    def availability(self) -> Tuple[bool, Optional[str]]:
        """The features' probe: unavailable while a configured directory is
        unsafe to clean (root_problem)."""
        problem = root_problem(self.roots(), self.staging_dirs())
        return (problem is None), problem

    def _3mf_outputs(self) -> Set[str]:
        converter = getattr(self.helix, "converter", None)
        records = getattr(converter, "records", None) or {}
        return {c.output for convs in records.values() for c in convs
                if getattr(c, "owned", False) and c.output}

    def free_bytes(self) -> Optional[int]:
        usage = disk_usage(str(self.helix.gc_path) if self.helix.gc_path else None)
        return usage["free"] if usage else None

    def _cleanable(self, consumer: Consumer) -> bool:
        """Photos are removed through helix_photos, so only while it runs."""
        if consumer.name == "photos":
            return getattr(self.helix, "photos", None) is not None
        return consumer.cleanable

    # -- guard ---------------------------------------------------------------

    def check_free_space(self, incoming_bytes: int) -> None:
        """Raise a 507 when writing `incoming_bytes` to the gcodes volume and
        printing from it would leave less than FREE_SPACE_FLOOR_BYTES free."""
        refusal = free_space_refusal(self.free_bytes(), incoming_bytes)
        if refusal is not None:
            logging.warning("HelixPrint: %s", refusal)
            raise self.server.error(refusal, 507)

    # -- report --------------------------------------------------------------

    async def report(self) -> Dict[str, Any]:
        roots = self.roots()
        usage = await self.eventloop.run_in_thread(
            scan_usage, roots, self.staging_dirs(), self._3mf_outputs())
        safe = self.availability()[0]
        consumers = []
        for consumer in CONSUMERS:
            entry = {"name": consumer.name, "root": consumer.root}
            entry.update(usage[consumer.name])
            entry["cleanable"] = bool(entry["cleanable"] and safe and self._cleanable(consumer))
            consumers.append(entry)
        return {
            "volume": disk_usage(roots[ROOT_GCODES]),
            "floor": FREE_SPACE_FLOOR_BYTES,
            "consumers": consumers,
            "auto_cleanup": {
                "enabled": await self.helix.feature_enabled(AUTO_CLEANUP_FEATURE),
                "last_run": self.last_auto_cleanup,
            },
        }

    async def handle_storage(self, web_request: Any) -> Dict[str, Any]:
        return await self.report()

    # -- cleanup -------------------------------------------------------------

    async def _printing_paths(self) -> Optional[Set[str]]:
        """The file Klipper is printing or holding paused, as named and as
        resolved through its link; None when Klipper cannot be asked."""
        apis = self.helix.klippy_apis
        if apis is None or self.helix.gc_path is None:
            return None
        try:
            result = await apis.query_objects({"print_stats": ["filename", "state"]})
        except Exception:
            return None
        stats = result.get("print_stats", {}) if isinstance(result, dict) else {}
        name = stats.get("filename") if isinstance(stats, dict) else None
        if not name or stats.get("state") in file_queue.IDLE_JOB_STATES:
            return set()
        path = os.path.join(str(self.helix.gc_path), name)
        return {os.path.abspath(path), os.path.realpath(path)}

    def _feature_paths(self) -> List[str]:
        """What power-loss recovery and a running analysis use: recovery's
        print file, link and layer indexes, analysis' index in progress."""
        paths = []  # type: List[str]
        plr = getattr(self.helix, "plr", None)
        if plr is not None:
            paths.extend(plr.protected_paths())
        analysis = getattr(self.helix, "analysis", None)
        if analysis is not None:
            paths.extend(analysis.protected_indexes())
        return paths

    async def protected_paths(self) -> Optional[Set[str]]:
        """Files no cleanup removes: an active linked print's copy and link,
        whatever Klipper is printing, and what power-loss recovery and
        analysis use. None when Klipper cannot say what it prints."""
        printing = await self._printing_paths()
        if printing is None:
            return None
        gc = str(self.helix.gc_path) if self.helix.gc_path is not None else ""
        paths = set(printing)
        for link, info in getattr(self.helix, "active_prints", {}).items():
            for p in (os.path.join(gc, link), os.path.join(gc, info.temp_filename)):
                paths.update((os.path.abspath(p), os.path.realpath(p)))
        for p in self._feature_paths():
            if p:
                paths.update((os.path.abspath(p), os.path.realpath(p)))
        return paths

    async def cleanup(self, consumer: Consumer, older_than: float) -> Dict[str, Any]:
        result = {"consumer": consumer.name, "removed": 0, "freed": 0, "errors": 0}
        roots = self.roots()
        problem = root_problem(roots, self.staging_dirs())
        if problem is not None:
            raise StorageError(problem)
        root = roots.get(consumer.root)
        if not root or not os.path.isdir(root):
            return result
        if consumer.name == "photos":
            photos = getattr(self.helix, "photos", None)
            if photos is None:
                raise StorageError("photos cannot be cleaned up without the photos feature")
            result.update(await photos.delete_older_than(max(older_than, consumer.min_age_s)))
            return result
        tops = cleanup_tops(consumer, root, self.staging_dirs())
        plr = getattr(self.helix, "plr", None)
        if consumer.name in ("helix_temp", "analysis_sidecars") and plr is not None \
                and plr.resume_unaccounted():
            # A snapshot waits whose files no record names: any copy or index
            # may be the one it resumes from.
            result["errors"] = 1
            logging.warning("HelixPrint: storage cleanup of %s skipped: a power-loss snapshot "
                            "waits with no job record", consumer.name)
            return result
        protected = await self.protected_paths()
        if protected is None:
            # Without knowing what prints, no file may be judged unused.
            result["errors"] = 1
            logging.warning("HelixPrint: storage cleanup of %s skipped: Klipper cannot say "
                            "what it prints", consumer.name)
            return result
        if consumer.name == "analysis_sidecars":
            await self.eventloop.run_in_thread(
                reader.prune_layer_indexes, root, self._feature_paths())
        files = await self.eventloop.run_in_thread(consumer_files, tops)
        chosen = cleanup_candidates(consumer, files, self.clock(), older_than, protected)
        async with self.helix.link_lock():
            removed, freed, errors = await self.eventloop.run_in_thread(
                remove_files, chosen, tops)
        result.update({"removed": removed, "freed": freed, "errors": errors})
        logging.info("HelixPrint: storage cleanup of %s older than %ss removed %d files "
                     "(%d bytes, %d errors)", consumer.name, older_than, removed, freed, errors)
        return result

    async def handle_storage_cleanup(self, web_request: Any) -> Dict[str, Any]:
        try:
            consumer = resolve_consumer(web_request.get_str("consumer", ""))
            older_than = parse_older_than(_arg(web_request, "older_than", 0))
        except StorageError as e:
            raise self.server.error(str(e), 400)
        if not self._cleanable(consumer):
            raise self.server.error("%s cannot be cleaned up here" % consumer.name, 409)
        try:
            return await self.cleanup(consumer, older_than)
        except StorageError as e:
            raise self.server.error(str(e), 409)

    # -- automatic cleanup ---------------------------------------------------

    def _schedule(self, delay: float) -> None:
        delay_callback = getattr(self.eventloop, "delay_callback", None)
        if delay_callback is not None:
            self._timer = delay_callback(delay, self.auto_cleanup)

    async def _job_idle(self) -> bool:
        apis = self.helix.klippy_apis
        if apis is None:
            return False
        try:
            result = await apis.query_objects({"print_stats": ["state"]})
        except Exception:
            return False
        stats = result.get("print_stats", {}) if isinstance(result, dict) else {}
        return isinstance(stats, dict) and stats.get("state") in file_queue.IDLE_JOB_STATES

    async def auto_cleanup(self) -> Optional[Dict[str, Any]]:
        """Remove every consumer's files older than its `auto_age_s`, when the
        user opted in and no print is running. Reschedules itself: a day on,
        or an hour on when a print kept it from running."""
        delay = AUTO_CLEANUP_INTERVAL_S
        try:
            if not await self.helix.feature_enabled(AUTO_CLEANUP_FEATURE):
                return None
            if not await self._job_idle():
                delay = AUTO_CLEANUP_BUSY_RETRY_S
                return None
            removed = freed = errors = 0
            for consumer in CONSUMERS:
                if consumer.auto_age_s is None:
                    continue
                result = await self.cleanup(consumer, consumer.auto_age_s)
                removed += result["removed"]
                freed += result["freed"]
                errors += result["errors"]
            self.last_auto_cleanup = {"time": self.clock(), "removed": removed, "freed": freed,
                                      "errors": errors}
            return self.last_auto_cleanup
        except Exception:
            logging.exception("HelixPrint: automatic storage cleanup failed")
            return None
        finally:
            self._schedule(delay)


def _arg(web_request: Any, key: str, default: Any) -> Any:
    getter = getattr(web_request, "get", None)
    if getter is None:
        return default
    try:
        return getter(key, default)
    except Exception:
        return default
