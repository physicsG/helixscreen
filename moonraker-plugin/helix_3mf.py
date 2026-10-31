# SPDX-License-Identifier: GPL-3.0-or-later
"""
HelixPrint .3mf converter.

A slicer with a "use 3MF" upload option sends `<name>.gcode.3mf`, a zip holding
the sliced plate as `Metadata/plate_<N>.gcode`. A Moonraker whose file manager
does not list `.3mf` as G-code stores the zip but can neither scan nor print it.
This converter writes each sliced plate out as a plain `.gcode` beside the zip,
through Moonraker's own upload path, so metadata, thumbnails and every client
treat it like any other print.

The rules (naming, ownership, cleanup) are documented in README.md, section
".3mf Conversion", together with the `server.helix.3mf_conversions` contract.

Pure helpers sit at the top and touch no Moonraker object; Helix3mfConverter
is the Moonraker-facing half and is driven by helix_print.py.
"""

from __future__ import annotations

import asyncio
import hashlib
import logging
import os
import shutil
import uuid
import zipfile
import zlib
from pathlib import Path
from typing import Any, Awaitable, Callable, Dict, Iterable, List, Optional, Tuple

import helix_file_queue as file_queue

# Database namespace holding one record list per source .3mf
DB_NAMESPACE = "helix_3mf"

# Owned outputs whose delete a running print refused, retried at job end.
DB_DEFERRED_NAMESPACE = "helix_3mf_deferred"
DB_DEFERRED_KEY = "outputs"

# Error of a plate whose conversion is under way.
_CONVERTING = "converting"

# Error of a plate whose planned name was taken during extraction; it is
# converted again straight away, under whatever name is free by then.
_RACE_ERROR = "a file arrived at the output name during conversion; converting again"

PLATE_PREFIX = "Metadata/plate_"
PLATE_SUFFIX = ".gcode"

# Inflate and hash in pieces this size: the whole plate is never in memory.
CHUNK_SIZE = 64 * 1024

# An md5 entry holds 32 hex digits; anything much larger is not one, and is
# not read into memory.
MD5_ENTRY_MAX = 64

# Extraction scratch files in temp_dir, published by a rename once complete.
PARTIAL_GLOB = "3mf-*.partial"

STATE_DONE = "done"
STATE_PENDING = "pending"
STATE_FAILED = "failed"

class ConversionError(Exception):
    """A .3mf that cannot be turned into G-code; the message is user-facing."""


# =============================================================================
# Pure helpers
# =============================================================================


def is_3mf(name: str) -> bool:
    return name.lower().endswith(".3mf")


def output_stem(source: str) -> str:
    """`dir/x.gcode.3mf` -> `dir/x`; `dir/x.3mf` -> `dir/x`."""
    lowered = source.lower()
    if lowered.endswith(".gcode.3mf"):
        return source[: -len(".gcode.3mf")]
    if lowered.endswith(".3mf"):
        return source[: -len(".3mf")]
    return source


def gcode_name_for(source: str) -> str:
    """The printable name a .3mf stands for: `dir/x.gcode.3mf` -> `dir/x.gcode`."""
    return output_stem(source) + PLATE_SUFFIX


def sliced_plates(names: Iterable[str]) -> List[int]:
    """Plate numbers with a sliced `Metadata/plate_<N>.gcode` entry, ascending."""
    plates = []
    for name in names:
        if not (name.startswith(PLATE_PREFIX) and name.endswith(PLATE_SUFFIX)):
            continue
        digits = name[len(PLATE_PREFIX): -len(PLATE_SUFFIX)]
        if digits.isdigit():
            plates.append(int(digits))
    return sorted(set(plates))


def plate_entry(plate: int) -> str:
    return f"{PLATE_PREFIX}{plate}{PLATE_SUFFIX}"


def file_md5(path: Path) -> str:
    digest = hashlib.md5()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(CHUNK_SIZE), b""):
            digest.update(chunk)
    return digest.hexdigest()


def read_plate_md5(zf: zipfile.ZipFile, plate: int) -> Optional[str]:
    """The slicer's own md5 of the plate, from `plate_<N>.gcode.md5`, if any."""
    try:
        info = zf.getinfo(plate_entry(plate) + ".md5")
    except KeyError:
        return None
    if info.file_size > MD5_ENTRY_MAX:
        return None
    raw = zf.read(info)
    text = raw.decode("ascii", errors="ignore").strip().lower()
    if len(text) == 32 and all(c in "0123456789abcdef" for c in text):
        return text
    return None


def extract_plate(zf: zipfile.ZipFile, plate: int, dest: Path) -> str:
    """Stream one plate's G-code to @p dest and return its md5.

    zipfile checks the entry's CRC as the last bytes are read; the slicer's md5,
    when the archive carries one, is checked on top. On any failure @p dest is
    removed and ConversionError raised.
    """
    digest = hashlib.md5()
    try:
        expected = read_plate_md5(zf, plate)
        with zf.open(plate_entry(plate)) as src, open(dest, "wb") as out:
            for chunk in iter(lambda: src.read(CHUNK_SIZE), b""):
                digest.update(chunk)
                out.write(chunk)
    except Exception as e:
        # zipfile raises RuntimeError for an encrypted entry,
        # NotImplementedError for a compression method it lacks and KeyError
        # for a missing one, besides BadZipFile/zlib.error/OSError for damage.
        _unlink_quietly(dest)
        raise ConversionError(f"plate {plate} could not be extracted: {_describe(e)}")
    md5 = digest.hexdigest()
    if expected is not None and md5 != expected:
        _unlink_quietly(dest)
        raise ConversionError(f"plate {plate} md5 {md5} does not match the slicer's {expected}")
    return md5


def _describe(e: BaseException) -> str:
    return f"{type(e).__name__}: {e}" if str(e) else type(e).__name__


def _unlink_quietly(path: Path) -> None:
    try:
        path.unlink()
    except OSError:
        pass


class Conversion:
    """One sliced plate of one source .3mf, as the endpoint reports it.

    `owned` is not part of the endpoint contract: it says the converter wrote
    `output` itself, as opposed to finding an identical file already there, and
    only an owned output is ever deleted.
    """

    FIELDS = (
        "source", "source_mtime", "source_size", "plate", "plate_md5",
        "output", "output_md5", "state", "error",
    )

    def __init__(
        self,
        source: str,
        source_mtime: float = 0.0,
        source_size: int = 0,
        plate: int = 0,
        plate_md5: str = "",
        output: str = "",
        output_md5: str = "",
        state: str = STATE_PENDING,
        error: str = "",
        owned: bool = False,
    ) -> None:
        self.source = source
        self.source_mtime = source_mtime
        self.source_size = source_size
        self.plate = plate
        self.plate_md5 = plate_md5
        self.output = output
        self.output_md5 = output_md5
        self.state = state
        self.error = error
        self.owned = owned

    def public(self) -> Dict[str, Any]:
        return {name: getattr(self, name) for name in self.FIELDS}

    def to_dict(self) -> Dict[str, Any]:
        record = self.public()
        record["owned"] = self.owned
        return record

    @classmethod
    def from_dict(cls, record: Dict[str, Any]) -> "Conversion":
        known = {k: record[k] for k in cls.FIELDS + ("owned",) if k in record}
        return cls(**known)


PlanAction = str  # "write" | "reuse" | "taken"


def plan_output(
    gc_path: Path,
    source: str,
    plate: int,
    multi: bool,
    plate_md5: str,
    records: Iterable[Conversion],
) -> Tuple[PlanAction, str, bool]:
    """Decide where a plate goes: (action, output, owned).

    `write` publishes the plate at `output`; `reuse` leaves an identical file
    there untouched; `taken` means every candidate name holds someone else's
    file. A file is ours only while a record of THIS source names it and its
    md5 still matches what we wrote: an edited output, or another source's
    output, is treated as foreign and never overwritten.
    """
    stem = output_stem(source)
    candidates = [] if multi else [stem + PLATE_SUFFIX]
    candidates.append(f"{stem}_plate{plate}{PLATE_SUFFIX}")
    records = list(records)
    for name in candidates:
        path = gc_path / name
        if not path.exists() and not path.is_symlink():
            return "write", name, True
        # A symlink is someone else's arrangement: never written through or
        # claimed, even when it points at an identical file.
        if path.is_symlink() or not path.is_file():
            continue
        md5 = file_md5(path)
        owner = next(
            (r for r in records
             if r.output == name and r.state in (STATE_DONE, STATE_PENDING)),
            None,
        )
        if owner is not None and owner.output_md5 == md5:
            if owner.source != source:
                continue
            if not owner.owned:
                if md5 == plate_md5:
                    return "reuse", name, False
                continue
            return ("reuse" if md5 == plate_md5 else "write"), name, True
        if md5 == plate_md5:
            return "reuse", name, False
    return "taken", "", False


upload_hook_shape_ok = file_queue.upload_hook_shape_ok


# =============================================================================
# Moonraker side
# =============================================================================


class _Request:
    def __init__(self) -> None:
        self.start_print = False
        self.user: Any = None
        self.waiters: List[asyncio.Future] = []
        # A held upload's end (event loop time): past it the slicer has had its
        # answer, so the conversion finishes without starting a print.
        self.deadline: Optional[float] = None
        self.abandoned = False
        # Set once its start call is under way: the hold then waits for it.
        self.starting = False

    def remaining(self) -> Optional[float]:
        if self.deadline is None:
            return None
        return max(0.0, self.deadline - asyncio.get_event_loop().time())


_print_result = file_queue.print_result

# Asked before every print this converter starts, with the output's
# gcodes-relative path and the seconds the upload may still be held (None:
# not held); False keeps the output uploaded and unstarted.
StartGate = Callable[[str, Optional[float]], Awaitable[bool]]


class Helix3mfConverter(file_queue.FileTracker):
    """Converts uploaded .3mf files, one at a time, off Moonraker's event loop."""

    def __init__(
        self,
        server: Any,
        file_manager: Any,
        database: Any,
        klippy_apis: Any,
        gc_path: Path,
        temp_dir: str,
        start_on_upload: bool,
        prints_3mf_natively: Callable[[Any], Optional[bool]],
    ) -> None:
        """@p prints_3mf_natively answers the capability question for this
        Moonraker (helix_capabilities.prints_3mf_natively): True, False or
        None when it cannot tell."""
        self.server = server
        self._prints_3mf_natively = prints_3mf_natively
        self.eventloop = server.get_event_loop()
        self.file_manager = file_manager
        self.database = database
        self.klippy_apis = klippy_apis
        self.gc_path = gc_path
        self.temp_dir = temp_dir
        self.start_on_upload = start_on_upload
        self.records: Dict[str, List[Conversion]] = {}
        self.active = False
        self.hook_installed = False
        self._queue = file_queue.WorkQueue(self._convert_queued, name=".3mf conversion")
        self._store = file_queue.RecordStore(
            lambda: self.database, DB_NAMESPACE, ".3mf conversion")
        self._original_finalize: Optional[Callable] = None
        self.start_gate: Optional[StartGate] = None
        # Whether an upload-and-print is held for the start gate now (the
        # gate's owner reads its opt-in); no hold, no gate and no deadline.
        self.hold_wanted: Optional[Callable[[], Awaitable[bool]]] = None
        # The request being converted, read by _start_print.
        self._active: Optional[_Request] = None
        # Owned outputs whose delete was refused because a print holds them.
        self._deferred_deletes: List[Conversion] = []

    # -- lifecycle -----------------------------------------------------------

    async def start(self) -> None:
        native = self._prints_3mf_natively(self.file_manager)
        if native:
            logging.info("HelixPrint: Moonraker prints .3mf natively, .3mf conversion off")
            return
        if native is None:
            logging.info(
                "HelixPrint: cannot read Moonraker's G-code extensions, converting .3mf anyway"
            )
        self.active = True
        await self.eventloop.run_in_thread(self._sweep_partials)
        await self._load_records()
        self.server.register_event_handler(
            "file_manager:filelist_changed", self.on_filelist_changed
        )
        self.server.register_event_handler(
            "job_state:state_changed", self.on_job_state_changed
        )
        if self.start_on_upload:
            self.install_upload_hook()
        await self.backfill()

    def _sweep_partials(self) -> int:
        """Remove extraction scratch files a crash or restart left in temp_dir.
        Runs before the queue starts, so none of them is in use."""
        removed = 0
        for partial in (self.gc_path / self.temp_dir).glob(PARTIAL_GLOB):
            if partial.is_file() and not partial.is_symlink():
                _unlink_quietly(partial)
                removed += 1
        if removed:
            logging.info(f"HelixPrint: removed {removed} unfinished .3mf extraction(s)")
        return removed

    def install_upload_hook(self) -> bool:
        method = getattr(self.file_manager, "finalize_upload", None)
        if not upload_hook_shape_ok(method):
            logging.warning(
                "HelixPrint: Moonraker's finalize_upload is not the expected "
                "`async (form_args)`; start_3mf_on_upload stays off, conversion continues"
            )
            return False
        self._original_finalize = method
        self.file_manager.finalize_upload = self._finalize_upload_hook
        self.hook_installed = True
        logging.info("HelixPrint: start_3mf_on_upload active")
        return True

    async def close(self) -> None:
        if self._original_finalize is not None:
            self.file_manager.finalize_upload = self._original_finalize
            self._original_finalize = None
            self.hook_installed = False
        self._queue.cancel()

    # -- endpoint ------------------------------------------------------------

    async def handle_conversions(self, web_request: Any) -> Dict[str, Any]:
        source = web_request.get_str("source", "") or ""
        if source:
            entries = self.records.get(source.lstrip("/"), [])
        else:
            entries = [c for group in self.records.values() for c in group]
        return {"conversions": [c.public() for c in entries]}

    # -- events --------------------------------------------------------------

    def tracking(self) -> bool:
        return self.active

    async def file_changed(self, path: str) -> None:
        if is_3mf(path):
            self.enqueue(path)

    async def file_deleted(self, path: str) -> None:
        if is_3mf(path):
            await self.remove_source(path)
        else:
            await self._forget_output(path)

    async def file_moved(self, old: str, new: str) -> None:
        if is_3mf(old) and old in self.records:
            await self._rekey(old, new)
            await self._save(new)
        elif is_3mf(new):
            self.enqueue(new)
        else:
            await self._move_output(old, new)

    async def on_job_state_changed(
        self, job_event: Any, prev_stats: Dict[str, Any], new_stats: Dict[str, Any]
    ) -> None:
        if not self.active or new_stats.get("state", "") not in file_queue.IDLE_JOB_STATES:
            return
        if self._deferred_deletes:
            deferred, self._deferred_deletes = self._deferred_deletes, []
            await self._drop_outputs(deferred, keep=())
            await self._save_deferred()
        for source, group in list(self.records.items()):
            if any(c.state == STATE_PENDING for c in group):
                self.enqueue(source)

    # -- queue ---------------------------------------------------------------

    def enqueue(
        self, source: str, start_print: bool = False, user: Any = None, front: bool = False
    ) -> asyncio.Future:
        """Queue @p source; the future resolves to _print_result(). With
        @p front it goes ahead of everything waiting (a slicer waiting on its
        upload response should not wait on a startup backfill)."""
        return self._enqueue(source, start_print, user, front)[1]

    def _enqueue(
        self, source: str, start_print: bool, user: Any, front: bool,
        deadline: Optional[float] = None,
    ) -> Tuple[_Request, asyncio.Future]:
        request = self._queue.put(source, _Request, front=front)
        if start_print:
            request.start_print = True
            request.user = user
            request.deadline = deadline
            # A re-send joining a request whose hold gave up gets its own chance.
            request.abandoned = False
        waiter = asyncio.get_event_loop().create_future()
        request.waiters.append(waiter)
        return request, waiter

    async def _convert_queued(self, source: str, request: _Request) -> None:
        outcome = _print_result()
        self._active = request
        try:
            outcome = await self.convert(source, request.start_print, request.user)
        except Exception:
            logging.exception(f"HelixPrint: .3mf conversion of {source} failed")
        finally:
            self._active = None
        for waiter in request.waiters:
            if not waiter.done():
                waiter.set_result(outcome)

    async def wait_idle(self) -> None:
        await self._queue.wait_idle()

    async def backfill(self) -> None:
        """Queue every .3mf with no up-to-date conversion; drop vanished sources."""
        found = await self.eventloop.run_in_thread(file_queue.scan_files, self.gc_path, is_3mf)
        for source in [s for s in self.records if s not in found]:
            await self.remove_source(source)
        for source in sorted(found):
            self.enqueue(source)

    # -- conversion ----------------------------------------------------------

    def _up_to_date(self, source: str, mtime: float, size: int) -> bool:
        group = self.records.get(source)
        if not group:
            return False
        for conv in group:
            if conv.source_mtime != mtime or conv.source_size != size:
                return False
            if conv.state == STATE_PENDING:
                return False
        return True

    async def convert(
        self, source: str, start_print: bool = False, user: Any = None
    ) -> Dict[str, bool]:
        """Convert one source; returns _print_result() for a requested print."""
        full = self.gc_path / source
        try:
            st = full.stat()
        except OSError:
            await self.remove_source(source)
            return _print_result()
        if self._up_to_date(source, st.st_mtime, st.st_size):
            self._mark_missing_outputs(source)
            await self._save(source)
            if start_print:
                return await self._start_existing(source, user)
            return _print_result()

        previous = self.records.get(source, [])
        try:
            plan = await self.eventloop.run_in_thread(self._read_plates, full)
        except ConversionError as e:
            if await self._source_vanished(source, full, []):
                return _print_result()
            self.records[source] = [
                Conversion(source, st.st_mtime, st.st_size, state=STATE_FAILED, error=str(e))
            ]
            await self._drop_outputs(previous, keep=())
            await self._save(source)
            logging.info(f"HelixPrint: {source}: {e}")
            return _print_result()

        # One pending entry per sliced plate before any plate is written: a
        # client deciding from the listing whether every plate of this archive
        # has a current output must never see some plates done and the rest
        # missing. The final entries replace these once all plates are through.
        self.records[source] = [
            Conversion(source, st.st_mtime, st.st_size, plate, plate_md5,
                       state=STATE_PENDING, error=_CONVERTING)
            for plate, plate_md5, _usize in plan
        ]
        await self._save(source)

        multi = len(plan) > 1
        results: List[Conversion] = []
        outcome = _print_result()
        for plate, plate_md5, usize in plan:
            conv = Conversion(source, st.st_mtime, st.st_size, plate, plate_md5)
            # Copies: plan_output reads them on a worker thread.
            known = [
                Conversion.from_dict(c.to_dict())
                for s, g in self.records.items() if s != source for c in g
            ] + [Conversion.from_dict(c.to_dict()) for c in previous]
            plan_args = (self.gc_path, source, plate, multi, plate_md5, known)
            action, output, owned = await self.eventloop.run_in_thread(plan_output, *plan_args)
            if action == "taken":
                conv.state = STATE_FAILED
                conv.error = "every output name holds a file the plugin did not write"
            elif action == "reuse":
                conv.output, conv.output_md5, conv.owned = output, plate_md5, owned
                conv.state = STATE_DONE
                if start_print and not multi:
                    outcome = await self._start_print(output, user)
            else:
                want_start = start_print and not multi
                # A held start publishes the output first, then asks the gate.
                gated = want_start and self._held()
                wrote = await self._write_output(
                    conv, full, output, usize, want_start and not gated, user, plan_args
                )
                if gated and conv.state == STATE_DONE:
                    outcome = await self._start_print(output, user)
                elif want_start:
                    outcome = wrote
            results.append(conv)
            if conv.state == STATE_FAILED:
                logging.info(f"HelixPrint: {source} plate {plate}: {conv.error}")
            elif conv.state == STATE_DONE:
                logging.info(f"HelixPrint: {source} plate {plate} -> {conv.output}")
        if start_print and multi:
            logging.info(f"HelixPrint: {source} holds {len(plan)} plates; not starting a print")

        if await self._source_vanished(source, full, results):
            return _print_result()
        self.records[source] = results
        await self._drop_outputs(previous, keep={c.output for c in results if c.output})
        await self._save(source)
        if any(c.state == STATE_PENDING and c.error == _RACE_ERROR for c in results):
            self.enqueue(source)
        return outcome

    async def _source_vanished(self, source: str, full: Path, results: List[Conversion]) -> bool:
        """After the awaits, whether the source was deleted meanwhile. Then the
        outputs just written are dropped instead of recorded: the delete event
        already cleaned up what was recorded before."""
        if full.exists():
            return False
        await self._drop_outputs(results, keep=())
        self.records.pop(source, None)
        await self._delete_record(source)
        logging.info(f"HelixPrint: {source} was deleted while converting")
        return True

    def _read_plates(self, full: Path) -> List[Tuple[int, str, int]]:
        """(plate, md5, uncompressed size) for every sliced plate, md5 from the
        archive when it carries one. Raises ConversionError for anything that
        stops the archive being read: a non-zip, a zip with no sliced plate,
        an encrypted md5 entry."""
        try:
            with zipfile.ZipFile(full) as zf:
                plates = sliced_plates(zf.namelist())
                if not plates:
                    raise ConversionError("no sliced plate in this .3mf (a project file?)")
                return [
                    (p, read_plate_md5(zf, p) or "", zf.getinfo(plate_entry(p)).file_size)
                    for p in plates
                ]
        except ConversionError:
            raise
        except Exception as e:
            raise ConversionError(f"not a readable .3mf: {_describe(e)}")

    async def _write_output(
        self,
        conv: Conversion,
        full: Path,
        output: str,
        usize: int,
        start_print: bool,
        user: Any,
        plan_args: Tuple[Any, ...],
    ) -> Dict[str, bool]:
        temp_dir = self.gc_path / self.temp_dir
        temp_dir.mkdir(parents=True, exist_ok=True)
        if shutil.disk_usage(str(temp_dir)).free < 2 * usize:
            conv.state, conv.error = STATE_FAILED, "not enough free space to convert"
            return _print_result()
        partial = temp_dir / PARTIAL_GLOB.replace("*", uuid.uuid4().hex)
        try:
            md5 = await self.eventloop.run_in_thread(self._extract, full, conv.plate, partial)
        except ConversionError as e:
            conv.state, conv.error = STATE_FAILED, str(e)
            return _print_result()
        # The name was planned before the extraction; a file can have arrived
        # there since (a user's upload of the same name). Publish only if the
        # plan still says this name is ours to write.
        again = await self.eventloop.run_in_thread(plan_output, *plan_args)
        if again != ("write", output, True):
            _unlink_quietly(partial)
            conv.state, conv.error = STATE_PENDING, _RACE_ERROR
            return _print_result()
        if conv.plate_md5 == "":
            conv.plate_md5 = md5
        directory, filename = os.path.split(output)
        form_args = {
            "root": "gcodes",
            "filename": filename,
            "path": directory,
            "tmp_file_path": str(partial),
            "print": "true" if start_print else "false",
        }
        if user is not None:
            form_args["current_user"] = user
        finalize = self._original_finalize or self.file_manager.finalize_upload
        try:
            result = await finalize(form_args)
        except Exception as e:
            _unlink_quietly(partial)
            if getattr(e, "status_code", None) == 403:
                # The print holds our earlier output: it stays ours until the
                # retry replaces it.
                conv.state, conv.error = STATE_PENDING, "waiting for the print using the output to end"
                held = self.gc_path / output
                if held.is_file():
                    conv.output, conv.owned = output, True
                    conv.output_md5 = await self.eventloop.run_in_thread(file_md5, held)
            else:
                conv.state, conv.error = STATE_FAILED, f"publishing the output failed: {e}"
            return _print_result()
        conv.output, conv.output_md5, conv.owned = output, md5, True
        conv.state, conv.error = STATE_DONE, ""
        result = result or {}
        return _print_result(
            bool(result.get("print_started")), bool(result.get("print_queued"))
        )

    @staticmethod
    def _extract(full: Path, plate: int, dest: Path) -> str:
        try:
            with zipfile.ZipFile(full) as zf:
                return extract_plate(zf, plate, dest)
        except ConversionError:
            raise
        except Exception as e:
            _unlink_quietly(dest)
            raise ConversionError(f"not a readable .3mf: {_describe(e)}")

    async def _start_existing(self, source: str, user: Any) -> Dict[str, bool]:
        group = [c for c in self.records.get(source, []) if c.state == STATE_DONE]
        if len(group) != 1 or len(self.records.get(source, [])) != 1:
            return _print_result()
        return await self._start_print(group[0].output, user)

    async def _start_print(self, output: str, user: Any) -> Dict[str, bool]:
        """Start @p output as Moonraker's own upload path would, once the
        start gate allows it when the upload is held. A held upload whose
        slicer already had its answer starts nothing."""
        request = self._active
        if self._held():
            try:
                allowed = await self.start_gate(
                    output, request.remaining() if request is not None else None)
            except Exception as e:
                logging.info(f"HelixPrint: start check of {output} failed ({e!r}); "
                             "starting unchecked")
                allowed = True
            if not allowed:
                return _print_result()
        if request is not None and request.abandoned:
            logging.info(f"HelixPrint: not starting {output}: its upload's hold ran out")
            return _print_result()
        if request is not None:
            request.starting = True
        return await file_queue.start_or_queue(
            self.server, self.klippy_apis, self.file_manager, output, user)

    # -- cleanup -------------------------------------------------------------

    def _mark_missing_outputs(self, source: str) -> None:
        for conv in self.records.get(source, []):
            if conv.state == STATE_DONE and not (self.gc_path / conv.output).is_file():
                self._mark_output_gone(conv)

    @staticmethod
    def _mark_output_gone(conv: Conversion) -> None:
        conv.state, conv.error = STATE_FAILED, "the output was deleted"
        conv.output, conv.output_md5, conv.owned = "", "", False

    async def remove_source(self, source: str) -> None:
        group = self.records.pop(source, None)
        if group is None:
            return
        await self._drop_outputs(group, keep=())
        await self._delete_record(source)

    async def _drop_outputs(self, group: Iterable[Conversion], keep: Iterable[str]) -> None:
        """Delete each output in @p group that we wrote and nobody changed."""
        keep = set(keep)
        for conv in group:
            if not conv.owned or not conv.output or conv.output in keep:
                continue
            path = self.gc_path / conv.output
            try:
                unchanged = path.is_file() and await self.eventloop.run_in_thread(
                    file_md5, path
                ) == conv.output_md5
            except OSError:
                unchanged = False
            if not unchanged:
                continue
            try:
                await self.file_manager.delete_file(f"gcodes/{conv.output}")
                logging.info(f"HelixPrint: removed {conv.output} (its .3mf is gone)")
            except Exception as e:
                if getattr(e, "status_code", None) == 403:
                    # A print holds it: retried when the job ends.
                    self._deferred_deletes.append(conv)
                    await self._save_deferred()
                    logging.info(f"HelixPrint: {conv.output} is in use, deleting it after the print")
                else:
                    logging.info(f"HelixPrint: keeping {conv.output}: {e}")

    async def _forget_output(self, output: str) -> None:
        for source, group in self.records.items():
            for conv in group:
                if conv.output == output and conv.state == STATE_DONE:
                    self._mark_output_gone(conv)
                    await self._save(source)
                    return

    async def _move_output(self, old: str, new: str) -> None:
        for source, group in self.records.items():
            for conv in group:
                if conv.output == old:
                    conv.output = new
                    await self._save(source)
                    return

    async def _rekey(self, old: str, new: str) -> None:
        group = self.records.pop(old)
        for conv in group:
            conv.source = new
        self.records[new] = group
        await self._delete_record(old)

    # -- persistence ---------------------------------------------------------

    async def _load_records(self) -> None:
        if self.database is None:
            return
        for source, group in (await self._store.load()).items():
            try:
                self.records[source] = [Conversion.from_dict(r) for r in group]
            except (TypeError, KeyError):
                logging.warning(f"HelixPrint: dropping unreadable .3mf record for {source}")
        try:
            deferred = await self.database.get_item(DB_DEFERRED_NAMESPACE, [DB_DEFERRED_KEY], [])
            self._deferred_deletes = [Conversion.from_dict(r) for r in deferred or []]
        except Exception as e:
            logging.warning(f"HelixPrint: could not read deferred .3mf output deletes: {e}")

    async def _save(self, source: str) -> None:
        if source in self.records:
            await self._store.save(source, [c.to_dict() for c in self.records[source]])

    async def _save_deferred(self) -> None:
        if self.database is None:
            return
        value = [c.to_dict() for c in self._deferred_deletes]
        try:
            await self.database.insert_item(DB_DEFERRED_NAMESPACE, [DB_DEFERRED_KEY], value)
        except Exception as e:
            logging.warning(f"HelixPrint: could not save deferred .3mf output deletes: {e}")

    async def _delete_record(self, source: str) -> None:
        await self._store.delete(source)

    # -- upload hook ---------------------------------------------------------

    async def _finalize_upload_hook(self, form_args: Dict[str, Any]) -> Dict[str, Any]:
        original = self._original_finalize
        assert original is not None
        request = file_queue.upload_print_request(form_args)
        if request is None or not (request.root == "gcodes" and is_3mf(request.filename)
                                   and request.wants_print):
            return await original(form_args)

        # A held upload (the start gate's owner opted in) gets one deadline
        # from here covering storing, conversion and the check. Without the
        # opt-in the print starts at whatever conversion time, as Moonraker's
        # own upload would.
        deadline = asyncio.get_event_loop().time() + file_queue.UPLOAD_HOLD_S
        if self.start_gate is None or self.hold_wanted is None or not await self.hold_wanted():
            deadline = None
        result, source = await file_queue.store_without_print(original, form_args)
        if not source:
            return result
        held, waiter = self._enqueue(
            source, True, form_args.get("current_user"), True, deadline=deadline)
        if deadline is None:
            outcome = await waiter
        else:
            outcome = await self._await_held(source, held, waiter, deadline)
        result = dict(result)
        result.update(outcome)
        return result

    def _held(self) -> bool:
        """Whether the request being converted is a held upload-and-print."""
        return self.start_gate is not None and self._active is not None \
            and self._active.deadline is not None

    async def _await_held(self, source: str, request: _Request, waiter: asyncio.Future,
                          deadline: float) -> Dict[str, bool]:
        """The outcome of a held upload, or "not started" once its deadline
        passes; the conversion then finishes without starting. A start call
        already under way at the deadline is waited for."""
        remaining = max(0.0, deadline - asyncio.get_event_loop().time())
        try:
            return await asyncio.wait_for(asyncio.shield(waiter), remaining)
        except asyncio.TimeoutError:
            pass
        if request.starting:
            return await waiter
        request.abandoned = True
        logging.info(f"HelixPrint: {source} not started: conversion and its start check "
                     f"took longer than {file_queue.UPLOAD_HOLD_S:.0f} s")
        return _print_result()
