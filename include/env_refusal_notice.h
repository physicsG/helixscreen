// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace helix {

/**
 * @brief One line the launcher skipped in helixscreen.env, as reported through
 *        HELIX_ENV_LINES_SKIPPED.
 *
 * @p label is the variable name, or "line N" for a line with no parsable key.
 */
struct EnvLineSkip {
    std::string label;
    std::string reason;
};

/**
 * @brief The launcher's whole-file refusal, as reported through
 *        HELIX_ENV_FILE_REFUSED ("kind|detail|expected|path").
 *
 * The launcher carries the classification, not prose: the app owns every
 * user-facing sentence, so the toast stays one line naming the single command
 * the actual problem needs.
 */
struct EnvFileRefusal {
    bool valid = false;
    std::string kind;     ///< mode | owner | chain | other
    std::string detail;   ///< offending uid (owner), short reason (other)
    std::string expected; ///< chown target (owner only)
    std::string path;     ///< the env file as configured
};

/**
 * @brief The two toast lines for a whole-file refusal.
 */
struct EnvRefusalCopy {
    std::string message; ///< What went wrong
    std::string fix;     ///< What to do about it
};

/**
 * @brief Label of the sentinel entry that closes a capped skipped-lines list.
 *
 * The launcher appends one entry with this label when it hits its cap, so the
 * count the app names is a lower bound. The label contains a space, so no
 * variable name or "line N" label can collide with it.
 */
inline constexpr const char* kEnvSkipSentinelLabel = "more skipped";

/**
 * @brief Split a HELIX_ENV_LINES_SKIPPED value into its entries.
 *
 * Entries are "label:reason" joined by `|` (the format
 * scripts/helix-launcher.sh builds and docs/devel/ENVIRONMENT_VARIABLES.md
 * documents). Malformed entries are dropped rather than fatal: the launcher
 * owns the format, and a notice that crashes the app over a stray entry is
 * the wrong trade.
 */
std::vector<EnvLineSkip> parse_env_lines_skipped(const std::string& value);

/**
 * @brief Parse a HELIX_ENV_FILE_REFUSED value.
 *
 * Same tolerance as parse_env_lines_skipped(): an unknown kind or a missing
 * field yields valid=false and no notification, and the launcher's log line
 * remains the record of what happened.
 */
EnvFileRefusal parse_env_file_refused(const std::string& value);

/**
 * @brief Map a refusal to its on-screen copy (English source, lv_tr'd).
 *
 * mode and owner name the one command the problem needs; chain and other
 * point at the log, which carries the launcher's full hint.
 */
EnvRefusalCopy env_refusal_copy(const EnvFileRefusal& refusal);

/**
 * @brief What the app should tell the user about the launcher's
 *        helixscreen.env handoff. An invalid refusal and no skipped lines
 *        mean: nothing to say.
 */
struct EnvRefusalNotice {
    EnvFileRefusal refusal;           ///< valid() when the whole file was refused
    std::size_t skipped_lines = 0;    ///< Parsed entries in @p skipped
    std::vector<EnvLineSkip> skipped; ///< Detail rows, one per skipped line
    /// True when @p skipped ends in the launcher's sentinel entry: the count
    /// is a lower bound, so the wording must say "at least N".
    bool skipped_truncated = false;
};

/**
 * @brief Decide what to tell the user from the two raw handoff values.
 *
 * Either argument may be null or empty: a clean file (and a dev run without
 * the launcher) exports neither variable.
 */
EnvRefusalNotice decide_env_refusal_notice(const char* file_refused, const char* lines_skipped);

/**
 * @brief Read the launcher's helixscreen.env handoff from the environment and
 *        notify the user through the boot-warning toast channel.
 *
 * Called once from Application startup. Before ToastManager::init() the
 * notification lands in PendingStartupWarnings and is drained as a toast
 * with the other boot warnings; no-op when the launcher exported nothing.
 */
void surface_env_refusal_from_launcher();

} // namespace helix
