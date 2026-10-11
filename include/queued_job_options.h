// Copyright (C) 2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file queued_job_options.h
 * @brief Per-job pre-print option state stored in the Moonraker database
 *
 * Moonraker's job queue carries no per-job data, so the options a user picked
 * when queueing a print (option row states from the file detail view) live in
 * namespace `helix-screen`, one database key per job:
 *
 *   queued_job_options.<job_id> =
 *       { "filename": "...", "options": { "<option_id>": bool } }
 *
 * Dotted keys are Moonraker's nested-record paths, so each job's entry is
 * written and deleted on its own key — two clients queueing concurrently
 * never read-modify-write the same record. Encode/decode are pure so they
 * test without LVGL; the store functions wire them to IMoonrakerAPI's
 * database get/post/delete.
 */

#pragma once

#include "i_moonraker_api.h"
#include "i_moonraker_sub_apis.h" // for JobQueueEntry
#include "json_fwd.h"

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace helix {
class AsyncLifetimeGuard;
}

namespace helix::queue {

/// Moonraker database namespace shared with tool_state.cpp's spool data
inline constexpr const char* kOptionsDbNamespace = "helix-screen";
/// Database key prefix; one job's entry lives at "<kOptionsDbKey>.<job_id>"
inline constexpr const char* kOptionsDbKey = "queued_job_options";

/// The database key holding @p job_id's entry
std::string queued_job_option_key(const std::string& job_id);

/// Options saved for one queued job
struct QueuedJobOptions {
    std::string filename;                ///< G-code filename as queued
    std::map<std::string, bool> options; ///< option row id -> saved state

    friend bool operator==(const QueuedJobOptions& a, const QueuedJobOptions& b) {
        return a.filename == b.filename && a.options == b.options;
    }
};

/// job_id -> saved state for that queued job
using QueuedJobOptionsMap = std::map<std::string, QueuedJobOptions>;

/// @brief Serialize one job's entry to its per-key database value shape
json encode_queued_job_entry(const QueuedJobOptions& entry);

/// @brief Parse one job's stored entry
///
/// Any malformed shape reads as defaults: not an object, a non-string
/// filename, a non-object options member or a non-boolean option state all
/// collapse to the whole-entry default rather than a half-parsed entry — the
/// store is advisory and a caller must never act on choices partially kept.
QueuedJobOptions decode_queued_job_entry(const json& value);

/// @brief Parse a whole stored map (the parent key's value, one entry per
/// child). Rows follow decode_queued_job_entry's all-or-nothing rule.
QueuedJobOptionsMap decode_queued_job_options(const json& value);

/// @brief Stored job ids whose job is no longer queued
///
/// The prune half as a pure rule: ids present in @p stored but absent from
/// @p queued_job_ids, sorted by key order.
std::vector<std::string>
stale_queued_job_option_ids(const QueuedJobOptionsMap& stored,
                            const std::vector<std::string>& queued_job_ids);

/// @brief Read the stored map, then the queue, delete every stale job's key
///
/// The two reads are ordered: the queue read is issued from the store read's
/// own completion, so it is at least as fresh as every entry the store
/// returned. A stored entry exists only once its add_job succeeded, which
/// means the later queue read names that job — an entry can only be deleted
/// when a queue read taken AFTER the store read does not name it, never
/// against a snapshot that predates the add.
///
/// Fire-and-forget and best-effort: a missing parent key (first run, fresh
/// database) and any read error leave the store untouched. The read
/// completions are marshalled through @p lifetime so a store whose owner died
/// mid-request never dereferences @p api.
///
/// @param lifetime Guard of the object owning @p api's lifetime
/// @param api API to read/write through; null is a no-op
void prune_stored_queued_job_options(AsyncLifetimeGuard& lifetime, IMoonrakerAPI* api);

/// @brief The job id present in @p after but not in @p before
///
/// post_job carries no echo of the created id, so it is recovered by diffing
/// the queue before the call against the queue the response reports.
/// nullopt when the answer is not exactly one id — an unchanged list, or two
/// new ids at once (another client queued concurrently): guessing there would
/// attach the saved options to someone else's job, so the caller skips the
/// save and the job stays queued without options.
std::optional<std::string> find_new_job_id(const std::vector<std::string>& before,
                                           const std::vector<JobQueueEntry>& after);

/// @brief Write one job's options to its own database key
///
/// Best-effort: the write is a single post_item of @p options under
/// queued_job_options.<job_id>, and a failure is logged, not surfaced — the
/// job is already queued and must stay so.
///
/// @param api API to write through; null is a no-op
/// @param job_id The newly queued job the options belong to
/// @param options Filename and option row states gathered at queue time
void save_queued_job_options(IMoonrakerAPI* api, const std::string& job_id,
                             QueuedJobOptions options);

/// @brief Read one job's stored options and hand them to @p on_loaded
///
/// Always answers exactly once, on the main thread, with defaults on any
/// miss: nothing stored, no entry for @p job_id, or a read error all deliver
/// an empty QueuedJobOptions — the caller cannot distinguish "never saved"
/// from "cannot read", which is the contract a start-with-saved-options flow
/// needs (it opens with defaults rather than refusing).
///
/// @param lifetime Guard of the object owning @p api's lifetime
/// @param api API to read through; null answers defaults immediately
/// @param job_id The queued job whose options to load
/// @param on_loaded Receives the stored entry, or defaults
void load_queued_job_options(AsyncLifetimeGuard& lifetime, IMoonrakerAPI* api,
                             const std::string& job_id,
                             std::function<void(QueuedJobOptions)> on_loaded);

/// @brief Delete one job's stored options
///
/// The counterpart of save_queued_job_options for the confirmed-start path:
/// the job left the queue, so its stored options must not outlive it. A
/// missing key is already the desired end state — Moonraker's 404 is
/// normalized to success by MoonrakerAPI::database_delete_item — and any
/// other failure is logged. Fire-and-forget.
///
/// @param api API to delete through; null is a no-op
/// @param job_id The job that just started printing
void delete_queued_job_options(IMoonrakerAPI* api, const std::string& job_id);

} // namespace helix::queue
