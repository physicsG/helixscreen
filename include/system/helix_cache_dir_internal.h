// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/**
 * @file helix_cache_dir_internal.h
 * @brief Internals of the cache cascade, exposed so the sweep is testable.
 *
 * Which rungs exist is a compile-time question (HELIX_PLATFORM_*), so a host
 * build can only ever exercise the desktop case through the public entry point.
 * Exposing the decision as a pure function over a candidate list lets a test
 * build the embedded shapes directly instead of mirroring the logic.
 */

#include <functional>
#include <string>
#include <vector>

namespace helix::cache_internal {

/// One rung of the cache cascade.
struct CacheCandidate {
    std::string path;
    /// Labels the rung in the log; nullptr means resolve quietly. Non-null also
    /// marks the rung deliberate - chosen on purpose, never reclaimed.
    const char* tier = nullptr;
    /// True for the compile-time platform rung. Its presence marks an embedded
    /// build; it need not win, since every device hook exports HELIX_CACHE_DIR.
    bool platform = false;
};

/// Env override, config setting, or platform path. Never reclaimed.
inline bool is_deliberate(const CacheCandidate& c) {
    return c.tier != nullptr;
}

/**
 * @brief Paths safe to reclaim, given one subdir's candidate list.
 *
 * Empty unless the list contains a platform rung. Returns the non-deliberate
 * rungs below the first viable one: those above were rejected as unusable,
 * those below were never probed.
 *
 * @param candidates The cascade for one subdir, in priority order.
 * @param viable     Predicate deciding whether a candidate could be used.
 */
std::vector<std::string> select_stale_paths(const std::vector<CacheCandidate>& candidates,
                                            const std::function<bool(const std::string&)>& viable);

/**
 * @brief Rename a state root whose name changed, keeping its contents.
 *
 * Moves @p legacy_root to @p current_root when only the old one exists. When
 * both exist (a half-finished migration), carries the known state subtrees
 * ("cache", "logs") across without clobbering, then drops the legacy side only
 * where it is empty — a platform hook still on the old layout recreates the
 * legacy "logs" dir at every pre-start, so an empty legacy tree must not
 * outlive the boot that renamed it. Never deletes a non-empty legacy subtree.
 *
 * Pure filesystem logic over the two paths: which roots a platform uses, and
 * any environment repair that has to go with the rename, live with the caller.
 *
 * @return Number of actions taken (renames, subtree moves, removals).
 */
int migrate_state_root(const std::string& legacy_root, const std::string& current_root);

} // namespace helix::cache_internal
