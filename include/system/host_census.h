// Copyright (C) 2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <optional>
#include <string>
#include <vector>

/**
 * @brief What else is running on this box, for a debug bundle
 *
 * "Two UIs are fighting for the framebuffer" and "the stock screen is still up"
 * reports need the distro, the failed systemd units and whoever holds the
 * display, none of which the rest of a bundle records
 * (prestonbrown/helixscreen#1692). Bounded and filtered on purpose: a full
 * process dump carries arguments, paths and device serials.
 */
namespace helix::diag {

/// A process worth reporting: a known competing UI, or a holder of a display device.
struct CensusProcess {
    long pid = 0;
    std::string name;                 ///< argv[0] basename only; arguments never leave
    std::vector<std::string> reasons; ///< "competing_ui:<needle>" and/or a held /dev path
};

struct HostCensus {
    std::string os_pretty_name; ///< /etc/os-release PRETTY_NAME, "" when absent
    /// Unset when unknown: no systemd, or systemctl gave no answer. Empty means
    /// systemd said nothing failed.
    std::optional<std::vector<std::string>> failed_units;
    std::vector<CensusProcess> processes;
};

/// The competing touchscreen UIs, matched as cmdline substrings. The installer
/// stops the same set (COMPETING_UIS in scripts/lib/installer/competing_uis.sh).
const std::vector<std::string>& competing_ui_names();

/// PRETTY_NAME from os-release content, unquoted; "" when it has none.
std::string os_release_pretty_name(const std::string& os_release);

/// Unit names from `systemctl list-units --plain --no-legend` output.
std::vector<std::string> parse_failed_units(const std::string& list_units_output);

/// The failed-unit answer from one systemctl run: unset when it exited
/// non-zero and printed nothing, since that is no answer at all.
std::optional<std::vector<std::string>> failed_units_from(int exit_status,
                                                          const std::string& output);

/// True when systemd is the running init under @p root: a systemctl binary
/// alone is not enough, since chroots, containers and SysV boxes ship one.
bool systemd_is_init(const std::string& root);

/// Competing UIs and /dev/fb* or /dev/dri/* holders under @p proc_root, at most
/// @p cap. A process that vanishes mid-walk is skipped; nothing here throws.
std::vector<CensusProcess> census_processes(const std::string& proc_root, size_t cap);

/// The census of the machine under @p root ("" = this one). systemctl is only
/// run against the live machine; under a test root it is detected, not run.
HostCensus collect_host_census(const std::string& root = "");

} // namespace helix::diag
