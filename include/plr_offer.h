// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace helix {

/// Raw inputs for the connect-time Power-Loss-Recovery offer decision, all
/// sourced from PrinterState (plus the controller's own latch).
///
/// `recovery_available` is the NORMALIZED signal that the backends collapse
/// into — see docs/devel/POWER_LOSS_RECOVERY.md and plr_backend.h:
///   - Snapmaker (passive): `virtual_sdcard.pl_env_valid == true`. That field
///     is emitted only by Snapmaker's forked virtual_sdcard (mainline/AFC
///     Klipper never sends it, and our parser only accepts a JSON boolean), so
///     it already means "Snapmaker firmware with a valid recovery snapshot" —
///     no separate backend/printer-type gate is needed, which is what lets the
///     offer fire on an AFC-modded U1 whose AMS backend is not the Snapmaker
///     backend.
///   - Qidi (passive): discovery saw Qidi's recovery macros AND
///     `save_variables.variables.was_interrupted` is a JSON boolean true. The
///     stock macros leave it true during every normal print too, so
///     `printer_idle` is what scopes the offer to a boot after power loss.
///   - Creality (active): the one-shot `check_continue_print_state` probe
///     completed and reported BOTH `file_state` and `eeprom_state` true.
///
/// Keeping the normalization outside this struct is what lets the latch,
/// re-arm, and wizard-suppression rules below stay backend-agnostic.
struct PlrOfferSignals {
    bool recovery_available; ///< a resumable snapshot exists (any backend)
    bool printer_idle;       ///< no active or paused print right now
    bool already_prompted;   ///< one-shot latch: already offered this connection
    bool wizard_active;      ///< setup wizard is running (app_globals::is_wizard_active())
};

/// Should HelixScreen offer to resume an interrupted print? Pure: no LVGL,
/// threading, or singletons. Returns false while the setup wizard is active
/// (`wizard_active`); the caller must NOT latch `already_prompted` on a
/// wizard-only suppression. The authoritative account of the one-shot latch and
/// how suppressed offers re-fire lives at the decision site,
/// PlrOfferController::evaluate_offer (ui_plr_offer_controller.cpp).
///
/// This function deliberately knows nothing about WHICH backend produced
/// `recovery_available`, and it is NOT the place that authorizes the resume
/// action — that gate is plr_build_plan() in plr_backend.h.
bool plr_should_offer(const PlrOfferSignals& signals);

/// Should the one-shot "already_prompted" latch be re-armed? True only on a
/// CONNECTED -> not-CONNECTED transition, so a disconnect/reconnect cycle
/// offers again instead of staying latched from the prior session.
/// Takes raw `helix::ConnectionState` values as int to keep this header free
/// of the MoonrakerClient dependency; callers pass the real enum values.
///
/// This edge also re-arms the Creality probe latch — the probe is once per
/// CONNECTION, not once per process.
bool plr_should_rearm(int prev_conn_state, int new_conn_state);

} // namespace helix
