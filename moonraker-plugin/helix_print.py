# SPDX-License-Identifier: GPL-3.0-or-later
"""
HelixPrint - Moonraker component for handling modified G-code files.

This component provides a single API endpoint that handles the complete workflow
for printing modified G-code while preserving original file attribution in
Klipper's print_stats and Moonraker's history.

API v2.0: Path-based interface
- Client uploads modified file first via standard Moonraker file upload
- Then calls print_modified with path to the already-uploaded file
- This avoids memory-intensive JSON payloads for large G-code files

Key features:
- Single API endpoint: POST /server/helix/print_modified
- Path-based interface (receives file path, not content)
- Symlink-based filename preservation (Klipper sees original name)
- Automatic history patching to record original filename
- Configurable cleanup of temporary files

Moonraker versions:
- Symlink attribution, temp tracking and cleanup work from v0.8.x up; v0.8.x
  persists through the namespace key-value API instead of a SQL table.
- Rewriting the finished history entry to the original filename needs v0.9.0,
  where History grew save_job() and auxiliary_data became a list of provider
  entries. Those arrived together, so probing for save_job() is the same test
  as probing for the list shape. Older installs keep the symlink's name in
  history and log one warning per print.

Configuration (moonraker.conf):
    [helix_print]
    enabled: True
    temp_dir: .helix_temp
    symlink_dir: .helix_print
    cleanup_delay: 86400
"""

from __future__ import annotations

import glob as glob_module
import json
import logging
import sys
import time
from pathlib import Path
from typing import TYPE_CHECKING, Any, Dict, List, Optional

if TYPE_CHECKING:
    from moonraker.common import RequestType, WebRequest
    from moonraker.confighelper import ConfigHelper
    from moonraker.server import Server

# Database table name for tracking temp files
HELIX_TEMP_TABLE = "helix_temp_files"

# Maximum age for cleaned database records before deletion (30 days)
DB_RECORD_MAX_AGE = 30 * 86400

# Plugin version - used for API version detection by clients
PLUGIN_VERSION = "1.1.0"

# Namespace for key-value storage fallback (Moonraker v0.8.x)
HELIX_NAMESPACE = "helix_temp_files"

# Provider name stamped on our entry in a history job's auxiliary_data
HELIX_AUX_PROVIDER = "helix_print"


class PrintInfo:
    """Tracks information about an active modified print."""

    def __init__(
        self,
        original_filename: str,
        temp_filename: str,
        symlink_filename: str,
        modifications: List[str],
        start_time: float,
    ) -> None:
        self.original_filename = original_filename
        self.temp_filename = temp_filename
        self.symlink_filename = symlink_filename
        self.modifications = modifications
        self.start_time = start_time
        self.job_id: Optional[str] = None
        self.db_id: Optional[int] = None


class HelixPrint:
    """
    Moonraker component for handling modified G-code files.

    Provides:
    - Single API endpoint for modified print workflow
    - Symlink-based filename preservation for print_stats
    - History patching to record original filename
    - Automatic cleanup of temp files
    """

    def __init__(self, config: ConfigHelper) -> None:
        self.server: Server = config.get_server()
        self.eventloop = self.server.get_event_loop()

        # Configuration options
        self.temp_dir = config.get("temp_dir", ".helix_temp")
        self.symlink_dir = config.get("symlink_dir", ".helix_print")
        self.cleanup_delay = config.getint("cleanup_delay", 86400)  # 24 hours
        self.enabled = config.getboolean("enabled", True)

        # Validate directory names don't contain path separators
        if "/" in self.temp_dir:
            raise config.error("temp_dir cannot contain path separators")
        if "/" in self.symlink_dir:
            raise config.error("symlink_dir cannot contain path separators")

        # Component references (resolved after init)
        self.file_manager: Optional[Any] = None
        self.history: Optional[Any] = None
        self.klippy_apis: Optional[Any] = None
        self.database: Optional[Any] = None

        # State tracking
        self.active_prints: Dict[str, PrintInfo] = {}
        self.gc_path: Optional[Path] = None

        # Database backend flags (mutually exclusive, one will be True after init)
        self._use_sqlite = False      # Moonraker 0.9+ with execute_db_command
        self._use_namespace = False   # Moonraker 0.8.x with insert_item/get_item

        # Register API endpoints
        self.server.register_endpoint(
            "/server/helix/print_modified",
            ["POST"],
            self._handle_print_modified,
        )
        self.server.register_endpoint(
            "/server/helix/status",
            ["GET"],
            self._handle_status,
        )

        # Register event handlers
        self.server.register_event_handler(
            "job_state:state_changed", self._on_job_state_changed
        )
        self.server.register_event_handler(
            "history:history_changed", self._on_history_changed
        )
        self.server.register_event_handler(
            "server:klippy_ready", self._on_klippy_ready
        )

        logging.info(
            f"HelixPrint v{PLUGIN_VERSION} initialized: temp={self.temp_dir}, "
            f"symlink={self.symlink_dir}, cleanup={self.cleanup_delay}s"
        )

    # =========================================================================
    # Input Validation
    # =========================================================================

    def _validate_filename(self, filename: str) -> None:
        """
        Validate filename for security issues.

        Raises server.error if validation fails.
        """
        if not filename:
            raise self.server.error("Filename cannot be empty", 400)

        # Check for null bytes and control characters
        if "\0" in filename or any(ord(c) < 32 for c in filename):
            raise self.server.error("Filename contains invalid characters", 400)

        # Check for absolute paths
        if filename.startswith("/"):
            raise self.server.error("Filename cannot be absolute path", 400)

        # Check for path traversal
        if ".." in filename:
            raise self.server.error("Filename cannot contain '..'", 400)

    def _validate_path_within_gcodes(self, path: Path) -> Path:
        """
        Resolve path and ensure it stays within gcodes directory.

        Returns resolved path if valid, raises server.error otherwise.
        """
        if self.gc_path is None:
            raise self.server.error("File manager not initialized", 500)

        # Resolve to absolute path (follows symlinks, resolves ..)
        try:
            resolved = path.resolve()
            gc_resolved = self.gc_path.resolve()
        except (OSError, RuntimeError) as e:
            raise self.server.error(f"Invalid path: {e}", 400)

        # Ensure resolved path is under gcodes directory
        try:
            resolved.relative_to(gc_resolved)
        except ValueError:
            raise self.server.error(
                "Path traversal detected: path escapes gcodes directory", 400
            )

        return resolved

    def _escape_gcode_string(self, s: str) -> str:
        """Escape a string for use in G-code commands."""
        # Remove any quotes that could break the command
        return s.replace('"', "").replace("'", "")

    # =========================================================================
    # Component Lifecycle
    # =========================================================================

    async def component_init(self) -> None:
        """Called after all components are loaded."""
        self.file_manager = self.server.lookup_component("file_manager")
        self.history = self.server.lookup_component("history", None)
        self.klippy_apis = self.server.lookup_component("klippy_apis")
        self.database = self.server.lookup_component("database")

        # Get gcodes path
        self.gc_path = Path(self.file_manager.get_directory("gcodes"))

        # Ensure directories exist
        await self._ensure_directories()

        # Initialize database table
        await self._init_database()

        # Schedule startup cleanup
        self.eventloop.register_callback(self._startup_cleanup)

    async def _ensure_directories(self) -> None:
        """Ensure temp and symlink directories exist."""
        if self.gc_path is None:
            return

        temp_path = self.gc_path / self.temp_dir
        symlink_path = self.gc_path / self.symlink_dir

        temp_path.mkdir(parents=True, exist_ok=True)
        symlink_path.mkdir(parents=True, exist_ok=True)

        logging.debug(
            f"HelixPrint: Ensured directories exist: {temp_path}, {symlink_path}"
        )

    async def _init_database(self) -> None:
        """Initialize database for tracking temp files.

        Tries SQLite API first (Moonraker 0.9+), then falls back to
        namespace key-value API (Moonraker 0.8.x).
        """
        if self.database is None:
            logging.warning(
                "HelixPrint: Database not available, persistence disabled"
            )
            return

        # Try SQLite API first (Moonraker 0.9+)
        if hasattr(self.database, "execute_db_command"):
            try:
                await self.database.execute_db_command(
                    f"""
                    CREATE TABLE IF NOT EXISTS {HELIX_TEMP_TABLE} (
                        id INTEGER PRIMARY KEY AUTOINCREMENT,
                        original_filename TEXT NOT NULL,
                        temp_filename TEXT NOT NULL,
                        symlink_filename TEXT NOT NULL,
                        modifications TEXT,
                        job_id TEXT,
                        created_at REAL NOT NULL,
                        cleanup_scheduled_at REAL,
                        status TEXT DEFAULT 'active'
                    )
                    """
                )
                self._use_sqlite = True
                logging.info("HelixPrint: Using SQLite database for persistence")
                return
            except Exception as e:
                logging.warning(f"HelixPrint: SQLite init failed: {e}, trying namespace API")

        # Fall back to namespace API (Moonraker 0.8.x)
        if hasattr(self.database, "insert_item"):
            self._use_namespace = True
            logging.info("HelixPrint: Using namespace API for persistence (Moonraker 0.8.x)")
            return

        logging.warning("HelixPrint: No compatible database API found, persistence disabled")

    # =========================================================================
    # API Handlers
    # =========================================================================

    async def _handle_status(self, web_request: WebRequest) -> Dict[str, Any]:
        """Handle status request - useful for plugin detection and version checking."""
        return {
            "enabled": self.enabled,
            "temp_dir": self.temp_dir,
            "symlink_dir": self.symlink_dir,
            "cleanup_delay": self.cleanup_delay,
            "active_prints": len(self.active_prints),
            "version": PLUGIN_VERSION,
        }

    async def _handle_print_modified(
        self, web_request: WebRequest
    ) -> Dict[str, Any]:
        """
        Handle the print_modified API request (v2.0 - path-based).

        This is the main entry point for printing modified G-code files.
        The client must upload the modified file first via standard Moonraker
        file upload, then call this endpoint with the path.

        Workflow:
        1. Client uploads modified file to .helix_temp/ via /server/files/upload
        2. Client calls this endpoint with temp_file_path
        3. Plugin validates paths, copies metadata, creates symlink
        4. Plugin starts print via symlink

        Parameters:
            original_filename: Path to the original G-code file (for history)
            temp_file_path: Path to the already-uploaded modified file
            modifications: List of modification identifiers for tracking
            copy_metadata: Whether to copy thumbnails from original (default: True)
        """
        if not self.enabled:
            raise self.server.error("HelixPrint component is disabled", 503)

        if self.gc_path is None:
            raise self.server.error("File manager not initialized", 500)

        # Get and validate parameters
        original_filename = web_request.get_str("original_filename")
        temp_file_path = web_request.get_str("temp_file_path")
        modifications = web_request.get_list("modifications", [])
        copy_metadata = web_request.get_boolean("copy_metadata", True)

        # Security validations
        self._validate_filename(original_filename)
        self._validate_filename(temp_file_path)

        # Validate original file exists and is within gcodes
        original_path = self.gc_path / original_filename
        original_resolved = self._validate_path_within_gcodes(original_path)

        if not original_resolved.exists():
            raise self.server.error(
                f"Original file not found: {original_filename}", 400
            )

        # Don't allow following symlinks for the original file
        if original_path.is_symlink():
            raise self.server.error(
                "Original file cannot be a symlink", 400
            )

        # Validate temp file exists and is within gcodes
        temp_path = self.gc_path / temp_file_path
        temp_resolved = self._validate_path_within_gcodes(temp_path)

        if not temp_resolved.exists():
            raise self.server.error(
                f"Temp file not found: {temp_file_path}. "
                "Upload the modified file first via /server/files/upload", 400
            )

        # Use the provided temp path (client already uploaded it)
        temp_filename = temp_file_path
        logging.info(f"HelixPrint: Using uploaded temp file {temp_filename}")

        # Copy metadata (thumbnails) from original
        if copy_metadata:
            await self._copy_metadata(original_resolved, temp_resolved)

        # The symlink mirrors the original's whole gcodes-relative path under a
        # `full/` segment, so the filename Klipper reports names the original on
        # its own. The segment tells clients this shape from the bare-filename
        # links earlier plugin versions made directly in the symlink directory.
        symlink_filename = f"{self.symlink_dir}/full/{original_filename}"
        symlink_path = self.gc_path / symlink_filename

        # Validate symlink path: it must land inside the symlink directory.
        resolved_parent = self._validate_path_within_gcodes(symlink_path.parent)
        try:
            resolved_parent.relative_to((self.gc_path / self.symlink_dir).resolve())
        except ValueError:
            raise self.server.error(
                "Path traversal detected: symlink escapes the symlink directory", 400
            )

        symlink_path.parent.mkdir(parents=True, exist_ok=True)

        # Create symlink atomically (handles race condition)
        try:
            self._create_symlink_atomic(symlink_path, temp_path)
            logging.info(
                f"HelixPrint: Created symlink {symlink_filename} -> {temp_filename}"
            )
        except Exception as e:
            # Clean up temp file and any directories made for the link
            temp_path.unlink(missing_ok=True)
            self._remove_symlink(symlink_path)
            raise self.server.error(f"Failed to create symlink: {e}", 500)

        # Track this print
        print_info = PrintInfo(
            original_filename=original_filename,
            temp_filename=temp_filename,
            symlink_filename=symlink_filename,
            modifications=modifications,
            start_time=time.time(),
        )
        self.active_prints[symlink_filename] = print_info

        # Persist to database for crash recovery
        await self._persist_print_info(print_info)

        # Start the print with symlink path (escape filename for G-code)
        safe_symlink = self._escape_gcode_string(symlink_filename)
        try:
            await self.klippy_apis.run_gcode(
                f'SDCARD_PRINT_FILE FILENAME="{safe_symlink}"'
            )
            logging.info(f"HelixPrint: Started print with {symlink_filename}")
        except Exception as e:
            # Clean up on print start failure
            self._remove_symlink(symlink_path)
            temp_path.unlink(missing_ok=True)
            del self.active_prints[symlink_filename]
            raise self.server.error(f"Failed to start print: {e}", 500)

        return {
            "original_filename": original_filename,
            "print_filename": symlink_filename,
            "temp_filename": temp_filename,
            "status": "printing",
        }

    def _remove_symlink(self, symlink_path: Path) -> None:
        """Remove a print symlink and any directories it leaves empty, up to
        (not including) the symlink directory."""
        if symlink_path.is_symlink():
            symlink_path.unlink()
        if self.gc_path is None:
            return
        root = self.gc_path / self.symlink_dir
        parent = symlink_path.parent
        while parent != root and root in parent.parents:
            try:
                parent.rmdir()
            except OSError:
                break  # not empty, or already gone
            parent = parent.parent

    def _create_symlink_atomic(self, symlink_path: Path, target_path: Path) -> None:
        """
        Create symlink atomically, handling existing files.

        Uses try/except pattern to avoid TOCTOU race conditions.
        """
        try:
            symlink_path.symlink_to(target_path)
        except FileExistsError:
            # Remove existing and retry
            if symlink_path.is_symlink() or symlink_path.exists():
                symlink_path.unlink()
            symlink_path.symlink_to(target_path)

    # =========================================================================
    # Metadata Handling
    # =========================================================================

    async def _copy_metadata(
        self, original_path: Path, temp_path: Path
    ) -> None:
        """Copy slicer metadata (thumbnails) from original to temp file."""
        if self.gc_path is None:
            return

        thumbs_dir = self.gc_path / ".thumbs"
        if not thumbs_dir.exists():
            return

        original_stem = original_path.stem
        temp_stem = temp_path.stem

        # Escape glob special characters in the stem
        escaped_stem = glob_module.escape(original_stem)

        # Find and link thumbnails for the original file
        for thumb in thumbs_dir.glob(f"{escaped_stem}*"):
            try:
                # Create symlink to original thumbnail with new name
                new_name = thumb.name.replace(original_stem, temp_stem)
                temp_thumb = thumbs_dir / new_name
                if not temp_thumb.exists():
                    temp_thumb.symlink_to(thumb)
                    logging.debug(
                        f"HelixPrint: Linked thumbnail {new_name} -> {thumb.name}"
                    )
            except Exception as e:
                logging.warning(f"HelixPrint: Failed to link thumbnail: {e}")

    # =========================================================================
    # Database Operations
    # =========================================================================

    async def _persist_print_info(self, print_info: PrintInfo) -> None:
        """Save print info to database for crash recovery."""
        record = {
            "original_filename": print_info.original_filename,
            "temp_filename": print_info.temp_filename,
            "symlink_filename": print_info.symlink_filename,
            "modifications": print_info.modifications,
            "created_at": time.time(),
            "cleanup_scheduled_at": None,
            "status": "active",
        }

        try:
            if self._use_sqlite:
                result = await self.database.execute_db_command(
                    f"""
                    INSERT INTO {HELIX_TEMP_TABLE}
                    (original_filename, temp_filename, symlink_filename,
                     modifications, created_at, status)
                    VALUES (?, ?, ?, ?, ?, ?)
                    """,
                    (
                        record["original_filename"],
                        record["temp_filename"],
                        record["symlink_filename"],
                        json.dumps(record["modifications"]),
                        record["created_at"],
                        record["status"],
                    ),
                )
                print_info.db_id = result.lastrowid
            elif self._use_namespace:
                # Use temp_filename as key (unique per print)
                await self.database.insert_item(
                    HELIX_NAMESPACE,
                    print_info.temp_filename,
                    record,
                )
        except Exception as e:
            logging.warning(f"HelixPrint: Failed to persist print info: {e}")

    # =========================================================================
    # Event Handlers
    # =========================================================================

    async def _on_history_changed(self, payload: Dict[str, Any]) -> None:
        """Record the history id of a print we staged.

        The id is history's own, assigned when it writes the row, and it is
        never part of Klipper's print_stats - so this event is the only place
        it can be read. It also carries the id in its payload rather than on
        the component, which matters: every handler of a Moonraker event runs
        under one asyncio.gather(), so a handler that reached into history for
        the live id would be racing history's own handler for it.
        """
        if payload.get("action") != "added":
            return
        job = payload.get("job") or {}
        print_info = self.active_prints.get(job.get("filename", ""))
        if print_info is None:
            return
        job_id = job.get("job_id")
        if not job_id:
            return
        print_info.job_id = job_id
        logging.info(f"HelixPrint: Job started with ID {job_id}")

    async def _on_klippy_ready(self) -> None:
        """Handle Klipper ready event - recover from any interrupted prints."""
        logging.debug("HelixPrint: Klipper ready, checking for interrupted prints")
        # Recovery logic would go here if needed

    async def _on_job_state_changed(
        self,
        job_event: Any,
        prev_stats: Dict[str, Any],
        new_stats: Dict[str, Any],
    ) -> None:
        """Handle job state changes to patch history."""
        state = new_stats.get("state", "")
        filename = new_stats.get("filename", "")

        # Check if this is one of our modified prints
        if not filename.startswith(f"{self.symlink_dir}/"):
            return

        print_info = self.active_prints.get(filename)
        if not print_info:
            logging.warning(f"HelixPrint: Unknown modified file: {filename}")
            return

        # Handle completion states
        if state in ("complete", "cancelled", "error"):
            logging.info(f"HelixPrint: Job finished ({state}): {filename}")

            # Patch history entry
            if self.history is not None:
                await self._patch_history_entry(print_info, state)

            # Schedule cleanup
            await self._schedule_cleanup(print_info)

            # Remove from active tracking
            del self.active_prints[filename]

    async def _patch_history_entry(
        self, print_info: PrintInfo, final_state: str
    ) -> None:
        """Rewrite the finished history entry so it names the original file.

        Cosmetic and best-effort: history shows the file the user picked
        instead of the symlink we printed through. Any failure is logged and
        swallowed so it can never surface in a print.
        """
        if not self.history or not print_info.job_id:
            return

        if not hasattr(self.history, "get_job") or not hasattr(
            self.history, "save_job"
        ):
            logging.warning(
                "HelixPrint: History filename-rename unavailable "
                "(history component has no get_job/save_job; needs Moonraker "
                "v0.9.0 or newer). The print itself is unaffected."
            )
            return

        # PrinterJob lives in whichever module defined the live history
        # component, so ask that module rather than guessing an import path.
        job_class = getattr(
            sys.modules.get(type(self.history).__module__), "PrinterJob", None
        )
        if job_class is None:
            logging.warning(
                "HelixPrint: History filename-rename unavailable "
                "(no PrinterJob alongside the history component)"
            )
            return

        try:
            job = await self.history.get_job(print_info.job_id)
            if not job:
                logging.warning(
                    f"HelixPrint: Job {print_info.job_id} not in history"
                )
                return

            # Strip the symlink dir prefix if present
            original = print_info.original_filename
            if original.startswith(f"{self.symlink_dir}/"):
                original = original[len(self.symlink_dir) + 1 :]

            # auxiliary_data is a list of provider entries. Keep every other
            # provider's, and drop any earlier entry of ours so re-patching the
            # same job does not stack duplicates.
            # auxiliary_data is a list of provider entries. Iterating anything
            # else would walk it by element anyway - a dict yields its KEYS -
            # and quietly write that back over the real thing, so a shape we do
            # not recognise is dropped rather than transformed.
            existing = job.get("auxiliary_data")
            if existing is not None and not isinstance(existing, list):
                logging.warning(
                    "HelixPrint: Ignoring auxiliary_data of unexpected type "
                    f"{type(existing).__name__}"
                )
                existing = None
            aux_data = [
                entry
                for entry in (existing or [])
                if not (
                    isinstance(entry, dict)
                    and entry.get("provider") == HELIX_AUX_PROVIDER
                )
            ]
            aux_data.append(
                {
                    "provider": HELIX_AUX_PROVIDER,
                    "name": "modifications",
                    "value": {
                        "modifications": print_info.modifications,
                        "temp_file": print_info.temp_filename,
                        "symlink": print_info.symlink_filename,
                        "original": print_info.original_filename,
                    },
                    "description": "G-code modifications applied by HelixScreen",
                    "units": None,
                }
            )

            job["filename"] = original
            job["auxiliary_data"] = aux_data

            # save_job REPLACEs the row addressed by job_id, and wants the
            # numeric id even though get_job also accepts the hex spelling.
            job_id = print_info.job_id
            numeric_id = (
                int(job_id, 16) if isinstance(job_id, str) else int(job_id)
            )
            await self.history.save_job(job_class(job), numeric_id)

            logging.info(
                f"HelixPrint: Patched history {print_info.job_id} "
                f"filename to '{original}'"
            )

        except Exception as e:
            logging.exception(f"HelixPrint: Failed to patch history: {e}")

    # =========================================================================
    # Cleanup Operations
    # =========================================================================

    async def _schedule_cleanup(self, print_info: PrintInfo) -> None:
        """Schedule cleanup of temp files after delay."""
        if self.gc_path is None:
            return

        # Immediately delete symlink (no longer needed)
        symlink_path = self.gc_path / print_info.symlink_filename
        self._remove_symlink(symlink_path)
        logging.debug(f"HelixPrint: Removed symlink {symlink_path}")

        # Also clean up thumbnail symlinks
        await self._cleanup_thumbnail_symlinks(print_info.temp_filename)

        # Update database status
        cleanup_time = time.time() + self.cleanup_delay
        try:
            if self._use_sqlite:
                await self.database.execute_db_command(
                    f"""
                    UPDATE {HELIX_TEMP_TABLE}
                    SET cleanup_scheduled_at = ?, status = ?
                    WHERE temp_filename = ?
                    """,
                    (cleanup_time, "pending_cleanup", print_info.temp_filename),
                )
            elif self._use_namespace:
                # Update record in namespace storage
                record = await self.database.get_item(
                    HELIX_NAMESPACE, print_info.temp_filename
                )
                if record:
                    record["cleanup_scheduled_at"] = cleanup_time
                    record["status"] = "pending_cleanup"
                    await self.database.update_item(
                        HELIX_NAMESPACE, print_info.temp_filename, record
                    )
                else:
                    logging.warning(
                        f"HelixPrint: Record not found for cleanup scheduling: "
                        f"{print_info.temp_filename}"
                    )
        except Exception as e:
            logging.warning(f"HelixPrint: Failed to update cleanup status: {e}")

        # Schedule delayed cleanup
        self.eventloop.delay_callback(
            self.cleanup_delay,
            self._cleanup_temp_file,
            print_info.temp_filename,
        )

        logging.info(
            f"HelixPrint: Scheduled cleanup of {print_info.temp_filename} "
            f"in {self.cleanup_delay}s"
        )

    async def _cleanup_thumbnail_symlinks(self, temp_filename: str) -> None:
        """Clean up thumbnail symlinks for a temp file."""
        if self.gc_path is None:
            return

        thumbs_dir = self.gc_path / ".thumbs"
        if not thumbs_dir.exists():
            return

        temp_stem = Path(temp_filename).stem

        # Escape glob special characters
        escaped_stem = glob_module.escape(temp_stem)

        for thumb in thumbs_dir.glob(f"{escaped_stem}*"):
            if thumb.is_symlink():
                thumb.unlink()
                logging.debug(f"HelixPrint: Removed thumbnail symlink {thumb}")

    async def _cleanup_temp_file(self, temp_filename: str) -> None:
        """Delete a temp file after cleanup delay."""
        if self.gc_path is None:
            return

        temp_path = self.gc_path / temp_filename
        file_deleted = False
        try:
            if temp_path.exists():
                temp_path.unlink()
                file_deleted = True
                logging.info(f"HelixPrint: Cleaned up {temp_filename}")
            else:
                # File already gone (manual deletion or previous cleanup)
                file_deleted = True
        except OSError as e:
            logging.error(f"HelixPrint: Failed to delete {temp_filename}: {e}")
            return  # Don't mark as cleaned if file delete failed

        # Only update database if file was actually deleted
        if not file_deleted:
            return

        try:
            if self._use_sqlite:
                await self.database.execute_db_command(
                    f"""
                    UPDATE {HELIX_TEMP_TABLE}
                    SET status = ?
                    WHERE temp_filename = ?
                    """,
                    ("cleaned", temp_filename),
                )
            elif self._use_namespace:
                record = await self.database.get_item(HELIX_NAMESPACE, temp_filename)
                if record:
                    record["status"] = "cleaned"
                    await self.database.update_item(
                        HELIX_NAMESPACE, temp_filename, record
                    )
                else:
                    logging.debug(
                        f"HelixPrint: No record to update for cleaned file: {temp_filename}"
                    )
        except Exception as e:
            logging.warning(f"HelixPrint: Failed to update cleanup status: {e}")

    async def _startup_cleanup(self) -> None:
        """Clean up stale temp files on startup."""
        if self.gc_path is None:
            return
        if not self._use_sqlite and not self._use_namespace:
            return

        now = time.time()

        try:
            # Get pending cleanup records
            pending_records: List[Dict[str, Any]] = []

            if self._use_sqlite:
                rows = await self.database.execute_db_command(
                    f"""
                    SELECT temp_filename, symlink_filename
                    FROM {HELIX_TEMP_TABLE}
                    WHERE status = 'pending_cleanup' AND cleanup_scheduled_at < ?
                    """,
                    (now,),
                )
                if rows:
                    pending_records = [dict(r) for r in rows]

            elif self._use_namespace:
                # Get all items and filter in Python
                try:
                    all_items = await self.database.ns_items(HELIX_NAMESPACE)
                except self.server.error:
                    # Namespace doesn't exist yet (no records inserted)
                    all_items = []
                for key, record in all_items:
                    if (
                        record.get("status") == "pending_cleanup"
                        and record.get("cleanup_scheduled_at")
                        and record["cleanup_scheduled_at"] < now
                    ):
                        pending_records.append(record)

            # Clean up each pending file
            cleaned_count = 0
            for record in pending_records:
                temp_filename = record["temp_filename"]
                symlink_filename = record["symlink_filename"]

                # Clean up files
                temp_path = self.gc_path / temp_filename
                symlink_path = self.gc_path / symlink_filename

                if temp_path.exists():
                    temp_path.unlink()
                self._remove_symlink(symlink_path)

                # Clean up thumbnail symlinks
                await self._cleanup_thumbnail_symlinks(temp_filename)

                # Update status
                if self._use_sqlite:
                    await self.database.execute_db_command(
                        f"""
                        UPDATE {HELIX_TEMP_TABLE}
                        SET status = ?
                        WHERE temp_filename = ?
                        """,
                        ("cleaned", temp_filename),
                    )
                elif self._use_namespace:
                    record["status"] = "cleaned"
                    await self.database.update_item(
                        HELIX_NAMESPACE, temp_filename, record
                    )
                cleaned_count += 1

            if cleaned_count > 0:
                logging.info(
                    f"HelixPrint: Startup cleanup removed {cleaned_count} stale files"
                )

            # Purge old database records to prevent unbounded growth
            purge_cutoff = now - DB_RECORD_MAX_AGE
            purged_count = 0

            if self._use_sqlite:
                deleted = await self.database.execute_db_command(
                    f"""
                    DELETE FROM {HELIX_TEMP_TABLE}
                    WHERE status = 'cleaned' AND created_at < ?
                    """,
                    (purge_cutoff,),
                )
                if deleted and deleted.rowcount > 0:
                    purged_count = deleted.rowcount

            elif self._use_namespace:
                # Get all items and delete old ones
                try:
                    all_items = await self.database.ns_items(HELIX_NAMESPACE)
                except self.server.error:
                    # Namespace doesn't exist yet
                    all_items = []
                for key, record in all_items:
                    if (
                        record.get("status") == "cleaned"
                        and record.get("created_at")
                        and record["created_at"] < purge_cutoff
                    ):
                        await self.database.delete_item(HELIX_NAMESPACE, key)
                        purged_count += 1

            if purged_count > 0:
                logging.info(
                    f"HelixPrint: Purged {purged_count} old database records"
                )

        except Exception as e:
            logging.exception(f"HelixPrint: Startup cleanup failed: {e}")


def load_component(config: ConfigHelper) -> HelixPrint:
    """Factory function to load the HelixPrint component."""
    return HelixPrint(config)
