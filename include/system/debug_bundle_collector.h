// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "hv/json.hpp"

namespace helix {

struct BundleOptions {
    bool include_klipper_logs = false;
    bool include_moonraker_logs = false;
    std::string user_note;
};

struct BundleResult {
    bool success = false;
    std::string share_code;
    std::string error_message;
};

/**
 * @brief Inputs to the bundle's `update` section.
 *
 * Plain data so the section can be assembled — and unit-tested — for both the
 * suppressed and not-suppressed cases without mutating the process-wide caches
 * behind updates_externally_managed() / self_update_supported().
 */
struct UpdateDiagnostics {
    std::string install_root;            ///< app_get_install_root() ("" if unresolvable)
    bool install_parent_writable = true; ///< dirname(install_root) writable WITHOUT escalation
    bool self_update_supported = true;   ///< self_update_supported(): writable OR root reachable
    bool externally_managed = false;     ///< updates_externally_managed()
    std::string channel;                 ///< "stable"|"beta"|"dev"; "" → "unknown"
    std::string r2_base_url;             ///< effective manifest base URL; "" → "unknown"
    std::string last_check_status;       ///< UpdateChecker::Status as a readable string
    std::string available_version;       ///< cached update version, "" if none
    std::string last_check_error;        ///< last check's error text, "" if none
    std::string platform_asset_name;     ///< exact release artifact this device requests
};

class DebugBundleCollector {
  public:
    /// Collect all debug data into JSON
    static nlohmann::json collect(const BundleOptions& options = {});

    /// Collect, compress, and upload asynchronously.
    /// Callback is invoked on the UI thread via helix::ui::queue_update().
    using ResultCallback = std::function<void(const BundleResult&)>;
    static void upload_async(const BundleOptions& options, ResultCallback callback);

    /// Individual collectors (public for testing)
    static nlohmann::json collect_system_info();
    static nlohmann::json collect_printer_info();
    /// `num_lines <= 0` (the default) ships the whole ring — see
    /// resolve_log_tail_lines().
    static std::string collect_log_tail(int num_lines = 0);

    /// How many lines collect_log_tail() actually asks for (public for testing).
    ///
    /// The ring's capacity scales with device RAM (logging_init.cpp
    /// ring_capacity_for_ram(): 16 lines/MB, clamped to [2000, 20000]), but the
    /// collector used to ask for a hardcoded 2000 — the *floor* of that range.
    /// So every device above the smallest boards paid the RAM to retain lines
    /// the bundle then discarded: on a 473 MB AD5X the ring holds 7568 and we
    /// shipped 26% of it, which on bundle LYGVE39Y meant 12 minutes of history
    /// against a 9-minute stall that started before the window opened.
    ///
    /// A positive `requested` is honoured verbatim. Otherwise ship the whole
    /// ring, falling back to the floor when no ring is installed (watchdog
    /// build, or before logging init) so the on-disk cascade stays bounded.
    static size_t resolve_log_tail_lines(int requested, size_t ring_capacity);

    /// Metadata about the log pipeline so a bundle reader knows whether debug
    /// was being captured: { target, level, ring_lines, log_tail_source }.
    static nlohmann::json collect_log_meta();

    /**
     * @brief In-app update diagnostics: why the update UI is (or is not) usable.
     *
     * The About screen gates both "Check for Updates" and "Install Update" on
     * !in_app_updates_suppressed(); when suppressed the rows are absent and the
     * user cannot update at all. Without this section a "cannot update" report
     * carries no evidence of whether that happened or which of the two
     * predicates caused it.
     *
     * No LVGL access — every value comes from a plain C++ getter, so this is
     * safe from the HttpExecutor thread that upload_async() collects on.
     */
    static nlohmann::json collect_update_info();

    /// Assemble the `update` section from explicit inputs. Pure and static so
    /// both suppression branches are unit-testable.
    static nlohmann::json build_update_info(const UpdateDiagnostics& diag);

    static std::string collect_crash_txt();
    static nlohmann::json collect_sanitized_settings();
    static std::string collect_klipper_log_tail(int num_lines = 2000);

    /// Read klippy.log / moonraker.log straight off the local disk, for when
    /// Moonraker cannot serve them. Only for a same-host printer; the paths come
    /// from the daemons' own argv (see candidate_log_paths()), so an unknown
    /// platform layout returns empty rather than a guess.
    static std::string collect_local_log_tail(const std::string& log_name, int num_lines,
                                              int condense_max_repeats = 0);
    /// Line cap for moonraker.log. Deliberately far above what the byte budget
    /// yields (a 2 MiB condensed window measured 4319 lines) so kMoonrakerTailBytes
    /// is what binds, not an arbitrary line count.
    static std::string collect_moonraker_log_tail(int num_lines = 8000);

    /// One entry from Moonraker's `logs` file root.
    struct LogFileEntry {
        std::string path; ///< relative to the logs root, may contain '/'
        uint64_t size = 0;
        double modified = 0.0;
    };

    /// Newest rotated predecessor of `stems` in a Moonraker `logs` listing, or ""
    /// (public for testing).
    ///
    /// Why this exists: a printer that just crashed is a printer that is about to
    /// be rebooted, and the reboot starts a fresh log. On bundle LYGVE39Y the
    /// incident lived entirely in the ROTATED moonraker.log.2026-08-11 while the
    /// active moonraker.log held nothing but the restart. We only ever fetched
    /// the active file, so the evidence was one HTTP GET away and we never made
    /// it.
    ///
    /// `stems` is a list because the klippy log is not called the same thing
    /// everywhere: Raspberry Pi installs use klippy.log, while AD5M/AD5X (and
    /// Vger1700's box) use printer.log. Rotated suffixes vary too — dated
    /// (`.2026-08-11`), dated+hour (`.2026-06-13_15`), and numeric (`.1`) all
    /// occur on real devices.
    ///
    /// Selection is deliberately narrow. A logs root holds other daemons' files,
    /// and on a live Pi `crowsnest.log.2026-08-11` is 940 KB and NEWER than every
    /// klippy rotation — so "newest rotated log" would ship a webcam log instead
    /// of the crash. Only `<stem>.<suffix>` at the root matches; nested paths
    /// (`mod/init.log.1`) and the active file itself never do.
    static std::string pick_rotated_sibling(const std::vector<LogFileEntry>& listing,
                                            const std::vector<std::string>& stems);

    /// GET /server/files/list?root=logs, parsed. Empty on any failure.
    static std::vector<LogFileEntry> fetch_log_listing(const std::string& base_url);

    /// Read crash_report.txt from config_dir (persists after crash.txt consumed)
    static std::string collect_crash_report_txt(const std::string& config_dir);

    /// Read raw crash.txt from config_dir (active crash, before next-boot rotation)
    static std::string collect_crash_txt(const std::string& config_dir);

    /// Read crash_history.json from config_dir (past crash submissions)
    static nlohmann::json collect_crash_history(const std::string& config_dir);

    /// Get double-hashed device ID from telemetry_device.json (for R2 cross-ref)
    static std::string collect_device_id(const std::string& config_dir);

    /// Read log tail from an explicit ordered list of paths (testable)
    static std::string collect_log_tail_from_paths(const std::vector<std::string>& paths,
                                                   int num_lines);

    /// Collect Moonraker state via REST (server info, printer state, config)
    static nlohmann::json collect_moonraker_info();

    /// Local evidence about a same-host Moonraker, gathered from /proc rather
    /// than from Moonraker. Everything in collect_moonraker_info() goes through
    /// Moonraker's own HTTP API, so a bundle uploaded while Moonraker is down
    /// carries only "No response" and cannot distinguish "not running" from
    /// "running, bound to an address we did not dial" (AD5X bundles TAU4PW4H /
    /// 865DXBQ7). Returns `{"same_host": false}` for a remote printer, where our
    /// own /proc says nothing about it.
    static nlohmann::json collect_moonraker_local_probe();

    /// Collect filament system data (AFC, Happy Hare, ACE, Spoolman, tool changers)
    static nlohmann::json collect_filament_system_info();

    /// Filter a Klipper object list to filament-related objects (public for testing)
    static nlohmann::json filter_filament_objects(const nlohmann::json& object_list);

    /// Shape-collapse threshold for klippy.log. Tuned against Klipper's
    /// per-second Stats line and ZMOD's 4-line toolhead dump.
    static constexpr int kKlipperCondenseMaxRepeats = 40;

    /// Shape-collapse threshold for moonraker.log. Higher than Klipper's because
    /// moonraker's most valuable repeated block is proc_stats._handle_shutdown()'s
    /// ~30 host-CPU samples, and shutdowns cluster: with 40, a second shutdown in
    /// the window starves the first one's block down to 10 samples (measured on
    /// Vger1700's moonraker.log.2026-08-11, which had two 102 lines apart). 100
    /// keeps every block whole while still collapsing the log_request() padding
    /// that dominates a busy file.
    static constexpr int kMoonrakerCondenseMaxRepeats = 100;

    /// Byte window fetched for moonraker.log. Raised from the 512 KiB default
    /// because condensing shrinks the payload afterwards, so a bigger fetch buys
    /// history rather than bundle size: on a real 1524 KiB moonraker.log, 512 KiB
    /// reached 7656 lines and shipped 167 KB, while 2 MiB reached all 23862 and
    /// shipped 240 KB. moonraker.log is also the log that SURVIVES the events
    /// klippy.log does not — it lives outside the Klipper tree, so a rollback or
    /// reinstall leaves it intact (bundle LYGVE39Y).
    static constexpr int kMoonrakerTailBytes = 2 * 1024 * 1024;

    /// Collapse repeating noise in a raw klippy.log tail (public for testing).
    ///
    /// Lines are grouped by "shape" (digit runs folded to N), and any shape
    /// occurring more than `max_repeats` times keeps only its most recent
    /// `max_repeats` occurrences, in place. The final `tail_lines` ship verbatim
    /// so the shutdown dump is never thinned.
    ///
    /// Deliberately shape-based rather than a list of known-noisy prefixes. The
    /// first version of this special-cased Klipper's per-second "Stats " line,
    /// which was the only padding in the sample it was written against — on a
    /// real ZMOD AD5X log the dominant noise turned out to be a 4-line
    /// `toolhead: max_velocity/max_accel/...` dump repeated 7916 times, which
    /// sailed straight through and made the shipped payload reach *less* far
    /// than the unfiltered tail it replaced. Shape-collapse catches both, and
    /// whatever the next firmware invents.
    ///
    /// Measured on two real AD5X logs: a 4 MiB (~85 min) window of the 11.7 MB
    /// crash log condenses 36142 lines to ~1300 (~340 KiB) while keeping the MCU
    /// shutdown and all 22 tool changes; a quiet 512 KiB sample keeps all 21 of
    /// its event lines.
    static std::string condense_klipper_log(const std::string& raw, int max_repeats = 40,
                                            int tail_lines = 200);

    /// Collect platform-specific diagnostic files (e.g., AD5X Adventurer5M.json)
    /// served via Moonraker's /server/files/<root>/<path> endpoint. Files that
    /// don't exist (404) are skipped silently; other errors are recorded in-line.
    static nlohmann::json collect_platform_files();

    /// Sanitize a string value for PII patterns (emails, credentials, webhooks, tokens, MACs)
    static std::string sanitize_value(const std::string& value);

    /// Recursively strip sensitive keys from JSON (public for integration testing)
    static nlohmann::json sanitize_json(const nlohmann::json& input, int depth = 0);

    /**
     * @brief Run sanitize_value() over each line of a multi-line body.
     *
     * Applied to every text section that leaves the machine. Line-at-a-time so
     * sanitize_value()'s 4 KB ReDoS guard does not redact a whole log as one
     * oversized value.
     *
     * Catches MACs, tokens, credentials and emails. It cannot catch an SSID —
     * that is an arbitrary user-chosen string with no pattern — so SSIDs are
     * kept out of the logs at the call site instead (include/log_redact.h).
     */
    static std::string sanitize_text_block(const std::string& body);

    /// Gzip compression using zlib
    static std::vector<uint8_t> gzip_compress(const std::string& data);

  private:
    static constexpr const char* WORKER_URL = "https://crash.helixscreen.org/v1/debug-bundle";
    static constexpr const char* INGEST_API_KEY = "hx-tel-v1-a7f3c9e2d1b84056";

    /// Blocking HTTP GET to a Moonraker endpoint, returns parsed JSON or error object
    static nlohmann::json moonraker_get(const std::string& base_url, const std::string& endpoint,
                                        int timeout_sec = 10);

    /// Get the Moonraker HTTP base URL (from IMoonrakerAPI if connected)
    static std::string get_moonraker_url();

    /// Fetch the tail of a log file from Moonraker using HTTP Range requests.
    ///
    /// `condense_max_repeats` of 0 ships the window verbatim; anything positive
    /// runs it through condense_klipper_log() at that threshold. The condenser is
    /// shape-based, not Klipper-specific, so moonraker.log uses it too — with its
    /// own threshold, see kMoonrakerCondenseMaxRepeats.
    static std::string fetch_log_tail(const std::string& base_url, const std::string& endpoint,
                                      int num_lines, int tail_bytes = 524288,
                                      int condense_max_repeats = 0);

    /// Prepend the newest rotated predecessor when the active log is too short to
    /// have used its byte budget. See the definition for why that predicate is
    /// the right trigger.
    static std::string prepend_rotated_predecessor(const std::string& base_url,
                                                   const std::vector<std::string>& stems,
                                                   const std::string& active_body, int tail_bytes,
                                                   int num_lines, int condense_max_repeats);

    /// Check if a key name matches a sensitive pattern
    static bool is_sensitive_key(const std::string& key);
};

} // namespace helix
