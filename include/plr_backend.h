// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "json_fwd.h"

#include <string>
#include <vector>

namespace helix {

class PrinterDiscovery;

/// Which firmware-specific Power-Loss-Recovery mechanism the connected printer
/// exposes. All three backends are SELF-GATING: each is chosen by markers only
/// that firmware ever carries, so no printer-model or AMS-backend gate is
/// needed (and none should be added — see tests/unit/test_plr_offer.cpp for
/// the AFC-modded-U1 regression).
///
/// Full mechanism writeup: docs/devel/POWER_LOSS_RECOVERY.md
enum class PlrBackendType {
    NONE = 0,  ///< No PLR support detected (mainline Klipper).
    SNAPMAKER, ///< Snapmaker U1 fork — virtual_sdcard.pl_env_valid (passive).
    CREALITY,  ///< Creality K/Ender/Hi fork — print_stats.power_loss (active probe).
    QIDI,      ///< Qidi Q2/Q1 Pro/Plus 4 stock macros, save_variables.was_interrupted (passive).
};

/// Capability markers harvested from the Moonraker status payload and the
/// discovery snapshot.
struct PlrCapabilitySignals {
    /// virtual_sdcard.pl_env_valid arrived as a JSON boolean AND is true.
    /// Snapmaker's fork is the only firmware that emits this key, and the value
    /// itself is the "a validated snapshot exists" signal, so capability and
    /// availability coincide for this backend.
    bool snapmaker_pl_env_valid = false;
    /// print_stats.power_loss arrived as a JSON NUMBER. Presence — not value —
    /// is the marker: the key exists only in Creality's fork and normally reads
    /// 0. Mainline Klipper omits it, and Moonraker answers a subscribed-but-
    /// unpopulated field with an explicit null, so "present and numeric" is what
    /// distinguishes the fork from everything else.
    bool creality_power_loss_field = false;
    /// Discovery found Qidi's recovery macros (plr_resume_macro_present()).
    /// They identify Qidi's stock firmware; without them a was_interrupted
    /// save_variable is just a user-writable name any Klipper could carry.
    bool qidi_resume_macro = false;
    /// save_variables.variables.was_interrupted arrived as a JSON boolean AND
    /// is true. The stock macros keep it true from PRINT_START until a normal
    /// end or cancel, so it is ALSO true during every normal print; the
    /// printer_idle input of the offer decision is what scopes it to a boot
    /// after power loss.
    bool qidi_was_interrupted = false;
};

/// Pick the backend. Passive markers outrank probe-based ones: SNAPMAKER
/// first, QIDI before CREALITY. No firmware carries more than one marker set,
/// so the ordering only makes a tie deterministic, and a passive signal
/// chosen by mistake costs no side-effectful probe.
PlrBackendType plr_select_backend(const PlrCapabilitySignals& caps);

/// Outcome of Creality's one-shot `pause_resume/check_continue_print_state`
/// probe. `completed` is the safety-critical field: it records that the probe
/// actually ran this connection AND returned a well-formed response. The probe
/// is what sets `print_stats.power_loss = 1` in firmware, which the stock
/// sensorless-homing macro reads to choose a full Z clearance lift before
/// homing. Resuming without it drags the nozzle through a tall part.
struct PlrDetectResult {
    bool completed = false;    ///< probe ran and parsed; NEVER set this by hand
    bool file_state = false;   ///< recovery sidecar present and coherent
    bool eeprom_state = false; ///< bl24c16f EEPROM snapshot present and coherent
};

/// A Creality recovery is offerable only when the probe completed AND BOTH
/// states are true. Do not weaken: without `eeprom_state` the Klipper-side
/// resume branch silently restarts the print FROM THE BEGINNING (the
/// `bl24c16f` object is absent, so `ISCONTINUEPRINT=1` degrades to a fresh
/// print rather than failing).
bool plr_creality_recovery_available(const PlrDetectResult& r);

/// Parse a `printer.pause_resume.check_continue_print_state` JSON-RPC response
/// into `out`. Sets `out.completed` only on a well-formed
/// `result.{file_state,eeprom_state}` pair of booleans. Returns false (leaving
/// `out.completed` false, i.e. resume-forbidden) on anything else.
bool plr_parse_check_continue_response(const nlohmann::json& response, PlrDetectResult& out);

/// The resolved resume/discard actions for one offer. Built once, at prompt
/// time, and carried by value into the modal so the button handlers never have
/// to re-derive the backend or re-check the safety invariant against live state
/// that may have moved on.
struct PlrRecoveryPlan {
    PlrBackendType backend = PlrBackendType::NONE;
    /// EMPTY means Resume must not be offered at all. For CREALITY that happens
    /// when the probe did not confirm both states, or when no recovery filename
    /// could be resolved (the gcode needs FILENAME=).
    std::string resume_gcode;
    std::string discard_gcode;      ///< gcode-based discard (Snapmaker)
    std::string discard_rpc_method; ///< JSON-RPC discard (Creality)
    std::string recovery_file;      ///< raw path/name, for the prompt body

    [[nodiscard]] bool resume_allowed() const {
        return !resume_gcode.empty();
    }
    [[nodiscard]] bool discard_available() const {
        return !discard_gcode.empty() || !discard_rpc_method.empty();
    }
};

/// Build the plan for `backend`. `detect` is consulted for CREALITY only, and
/// is the ONLY thing that can authorize a Creality resume — see PlrDetectResult.
/// An unsafe or empty `recovery_file` also suppresses the Creality resume,
/// since its gcode embeds the name as a parameter.
PlrRecoveryPlan plr_build_plan(PlrBackendType backend, const std::string& recovery_file,
                               const PlrDetectResult& detect);

/// Reject filenames that could break out of `SDCARD_PRINT_FILE FILENAME="<f>"`.
/// The value is emitted double-quoted, so spaces are fine; `"` and `\` (shlex's
/// quote and escape characters), comment/terminator characters and `=` are not.
bool plr_is_safe_recovery_filename(const std::string& name);

/// Convert the sidecar's ABSOLUTE `file_path` into the name
/// `SDCARD_PRINT_FILE` expects: relative to the virtual_sdcard root. Klipper
/// matches FILENAME against a file list built relative to that root (an
/// absolute path merely loses its leading `/` and then misses), and on the
/// resume path the reopened file's absolute name is compared back against the
/// sidecar's `file_path` — a mismatch discards the recovery data and restarts
/// the print. An already-relative name is returned unchanged.
std::string plr_creality_sdcard_relative_name(const std::string& path);

/// Extract `file_path` from the Creality recovery sidecar JSON. Returns an
/// empty string for malformed input or a missing/non-string key.
std::string plr_parse_creality_sidecar(const std::string& json_text);

/// Best-effort read of the Creality recovery filename from the on-device
/// sidecar. HelixScreen runs on the printer, so this normally succeeds; when it
/// does not the caller must degrade (no Creality resume is possible without a
/// filename). Never writes to the sidecar.
std::string plr_read_creality_recovery_filename();

// --- Wire constants -------------------------------------------------------

/// Snapmaker fork gcode.
inline constexpr const char* SNAPMAKER_RESUME_GCODE = "SDCARD_PRINT_PL_RESTORE";
inline constexpr const char* SNAPMAKER_DISCARD_GCODE = "SDCARD_PRINT_PL_CLEAR_ENV";

/// Creality fork. The detect endpoint is a Klipper webhook that Moonraker
/// auto-registers as a JSON-RPC method (klippy_connection.py re-exports every
/// non-reserved endpoint). It is SIDE-EFFECTFUL — call at most once per
/// connection, only while print_stats.state == "standby". Never poll it.
inline constexpr const char* CREALITY_DETECT_RPC =
    "printer.pause_resume.check_continue_print_state";
inline constexpr const char* CREALITY_DISCARD_RPC = "printer.pause_resume.cancel_continue_print";

/// Sidecar holding the interrupted job's path, relative to the data root.
inline constexpr const char* CREALITY_SIDECAR_REL_PATH =
    "creality/userdata/config/print_file_name.json";

/// Qidi stock firmware (Q2 / Q1 Pro / Plus 4). Both are the plain gcode
/// scripts the stock screen's Yes/No buttons send; neither takes parameters,
/// so a recovery filename is display-only for this backend. RESUME_INTERRUPTED
/// rebuilds the resume gcode from the .temp/ backup, lifts Z clear of the part
/// before homing X/Y, then prints it.
inline constexpr const char* QIDI_RESUME_GCODE = "RESUME_INTERRUPTED";
inline constexpr const char* QIDI_DISCARD_GCODE = "CLEAR_LAST_FILE";
/// The boot-time check the stock screen sends. Newer Qidi firmware (Q2
/// 01.01.02.03 at least) registers RESUME_INTERRUPTED and CLEAR_LAST_FILE as Python commands, which
/// never appear as gcode_macro objects, so this macro is the one that marks
/// every Qidi firmware generation.
inline constexpr const char* QIDI_DETECT_MACRO = "DETECT_INTERRUPTION";

/// Whether the connected printer runs a firmware whose resume command is one
/// the PLR module knows (currently Qidi's stock RESUME_INTERRUPTED, recognised
/// by DETECT_INTERRUPTION or a RESUME_INTERRUPTED macro - the macro is the
/// firmware discriminator, because the was_interrupted save_variable it
/// maintains is user-writable on ANY Klipper). Capability-named on purpose:
/// generic modules (PrinterState, the subscription builder) call this without
/// learning the vendor. Out-of-line to keep this header free of the
/// PrinterDiscovery dependency.
bool plr_resume_macro_present(const PrinterDiscovery& hw);

/// Status objects the PLR backends need subscribed for the discovered
/// firmware, empty when none do (the subscription builder loops over this and
/// never learns the vendor). Currently: save_variables, whose
/// variables.was_interrupted the passive Qidi backend reads.
std::vector<std::string> plr_required_status_objects(const PrinterDiscovery& hw);

/// Read the passive backend's interrupted flag out of a status payload:
/// 1/0 when save_variables.variables.was_interrupted arrived as a JSON boolean,
/// -1 when this frame says nothing about it (no save_variables key, no
/// variables dict, no such entry, or a non-boolean value - Moonraker notifies
/// at top-level-field granularity, so absence in a delta is silence, not
/// false). Booleans only: save_variables values are Python literals Klipper
/// re-parses, and another type under this name is not our signal.
int plr_parse_interrupted_flag(const nlohmann::json& status);

/// True when this status frame carries print_stats.power_loss as a JSON number.
/// Presence, not value, is the marker (the key exists only in the firmware
/// fork that supports the active probe, and normally reads 0); a Moonraker
/// null for a subscribed-but-unpopulated field does not count.
bool plr_parse_power_loss_signal(const nlohmann::json& status);

} // namespace helix
