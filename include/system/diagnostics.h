// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/**
 * @file diagnostics.h
 * @brief The resolved runtime facts a support case needs, collected once.
 *
 * Every important path in the app is chosen by a cascade and every rung of
 * every cascade is user-overridable, so the rule that computes a path says
 * nothing about where the file actually landed. This collects the RESOLVED
 * values — and, for the cache, which rung won.
 *
 * Typed fields, never rendered text: the startup log, the About panel and the
 * debug bundle each render it their own way, and only the bundle redacts.
 */

#include <cstdint>
#include <string>

namespace helix::diagnostics {

/// Where this process actually reads and writes.
struct Paths {
    std::string install_root;
    std::string config_dir;
    std::string settings_file; ///< the settings.json Config loaded, "" if none
    /// Where the cache cascade resolves to, read without creating it. The
    /// cache lands here unless creation fails at first use, which
    /// get_helix_cache_dir() reports as a [CacheDir] fall-through.
    std::string cache_dir;
    /// Which rung of the cache cascade won: "HELIX_CACHE_DIR", "config",
    /// "AD5M", "CC1", "K2", "MIPS", "Android", or "" for a fall-through rung
    /// (XDG, $HOME, /var/tmp, /tmp).
    std::string cache_tier;
    std::string state_dir; ///< backup/state root (settings.json.backup lives here)
    std::string log_file;  ///< "" unless the file sink is the active target
    /// TMP_DIR handed to install.sh for a download staged beside the install
    /// tree. A download that wins the free-space sweep elsewhere stages beside
    /// that location instead.
    std::string updater_staging_dir;
};

/// What this build is, and what it is driving.
struct Identity {
    /// Release-asset key from helix::platform::current_key(). One key can
    /// serve several filesystem layouts — "mips" is the K1 series and both
    /// AD5X firmware populations — which is what mod_flavor separates.
    std::string platform_key;
    std::string mod_flavor;    ///< "none", "ZMOD" or "Forge-X"
    std::string printer_model; ///< printer type as configured in HelixScreen
    std::string host_arch;     ///< helix::host_arch_string()
};

/// Host facts from /proc and uname. Any field whose source is missing keeps
/// its placeholder rather than an empty string.
struct Machine {
    std::string cpu_model = "unknown";
    int cpu_cores = 0;
    uint64_t mem_total_kb = 0;
    uint64_t mem_available_kb = 0;
    double uptime_seconds = 0.0;
    std::string loadavg = "unknown";
    std::string kernel_release = "unknown";
    std::string kernel_arch = "unknown";
};

/// Where log lines go and how much of them.
struct Logging {
    std::string destination; ///< human-readable active target
    std::string level;       ///< level the persistent sinks run at
    std::string file;        ///< resolved file-sink path, "" for other targets
};

struct Diagnostics {
    Paths paths;
    Identity identity;
    Machine machine;
    Logging log;
    /// Moonraker HTTP base URL in use. A LAN address: the bundle consumer runs
    /// it through DebugBundleCollector::sanitize_value(), the local consumers
    /// do not.
    std::string moonraker_url;
};

/// Collect everything. Cheap and side-effect free: stat() and small /proc
/// reads, no statvfs, and nothing on disk is created.
Diagnostics collect();

/**
 * @brief TMP_DIR install.sh receives for an archive staged beside @p install_root.
 *
 * "" when the root is unknown. Separate from collect() because the rule — which
 * archive location the updater asks about — is worth pinning on its own, and
 * the test binary has no resolvable install layout to reach it through.
 */
std::string staging_dir_for(const std::string& install_root);

/**
 * @brief Machine facts, read from @p proc_root.
 *
 * The parameter is the test seam: pointing it at a directory with no
 * meminfo/cpuinfo/uptime/loadavg exercises the degraded path that an embedded
 * board with a partial /proc produces.
 */
Machine read_machine(const std::string& proc_root = "/proc");

/**
 * @brief Write the collected facts to the log as a block, at INFO.
 *
 * The startup consumer. Called once, after logging init, so a log or a journal
 * excerpt carries the resolved layout even when no bundle was ever uploaded.
 * Local output: nothing here is redacted.
 */
void log_diagnostics();

} // namespace helix::diagnostics
