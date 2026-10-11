// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "printer_switch_flow.h"

#include "ui_effects.h"
#include "ui_modal.h"
#include "ui_toast_manager.h"
#include "ui_utils.h"
#include "ui_wizard.h"

#include "app_globals.h"
#include "boot_crash_guard.h"
#include "config.h"
#include "connection_state.h"
#include "observer_factory.h"
#include "print_lifecycle_state.h"
#include "printer_cache_registry.h"
#include "printer_state.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <string>
#include <utility>

#include "hv/json.hpp"

namespace helix {

namespace {

/// RAII latch for PrinterSwitchFlow::m_soft_restart_in_progress.
///
/// Clearing the re-entrancy flag by hand on each exit path leaves it stuck true whenever an
/// exit is not the one that was hand-coded, and a stuck flag makes every later printer
/// switch or add a silent no-op until the process restarts.
class SoftRestartLatch {
  public:
    explicit SoftRestartLatch(bool& flag) : m_flag(flag) {
        m_flag = true;
    }
    ~SoftRestartLatch() {
        m_flag = false;
    }

    SoftRestartLatch(const SoftRestartLatch&) = delete;
    SoftRestartLatch& operator=(const SoftRestartLatch&) = delete;
    SoftRestartLatch(SoftRestartLatch&&) = delete;
    SoftRestartLatch& operator=(SoftRestartLatch&&) = delete;

  private:
    bool& m_flag;
};

} // namespace

namespace {

/// A job the user would be walking away from: preparing, printing or paused.
bool active_printer_is_printing() {
    return PrintLifecycleState::is_active(get_printer_state().print_state().get_print_lifecycle());
}

} // namespace

bool printer_connection_live() {
    const auto state = static_cast<ConnectionState>(lv_subject_get_int(
        get_printer_state().network_state().get_printer_connection_state_subject()));
    return state != ConnectionState::DISCONNECTED && state != ConnectionState::FAILED;
}

PrinterSwitchFlow::PrinterSwitchFlow(Config*& config, AsyncLifetimeGuard& async, Restart restart)
    : m_config(config), m_async(async), m_restart(std::move(restart)) {}

PrinterSwitchFlow::~PrinterSwitchFlow() {
    dismiss_interstitial();
}

void PrinterSwitchFlow::show_interstitial(const std::string& title, const char* phase) {
    dismiss_interstitial();
    // On the top layer, which the restart does not tear down. Recreated per phase rather
    // than updated: the teardown stops every animation, the spinner's included.
    // Limited tiers get a still spinner: a moving one there tears.
    const char* animated = ui::full_style_effects_active() ? "true" : "false";
    const char* attrs[] = {"name",     "printer_switch_interstitial",
                           "title",    title.c_str(),
                           "phase",    phase,
                           "animated", animated,
                           nullptr};
    m_interstitial =
        static_cast<lv_obj_t*>(lv_xml_create(lv_layer_top(), "printer_switch_interstitial", attrs));
    if (!m_interstitial) {
        spdlog::error("[PrinterSwitchFlow] printer_switch_interstitial creation failed");
    }
}

void PrinterSwitchFlow::paint_loading_card(const std::string& title) {
    // The restart that follows blocks this thread for seconds, so the card paints now.
    show_interstitial(title, lv_tr("Loading..."));
    lv_refr_now(nullptr);
}

void PrinterSwitchFlow::await_connection(const std::string& title, bool connecting) {
    if (!connecting || is_wizard_active()) {
        dismiss_interstitial();
        return;
    }
    show_interstitial(title, lv_tr("Connecting..."));
    // State changes arrive queued, so the value on attach and the first ones after it can
    // still be the previous printer's, CONNECTED or FAILED alike. The new attempt shows
    // itself first: a fresh scope goes CONNECTING, a retargeted one DISCONNECTED. Only
    // after that does CONNECTED or FAILED belong to the new printer; FAILED hands over to
    // the connection-failed UI.
    m_connect_observer = ui::observe<int>(
        get_printer_state().network_state().get_printer_connection_state_subject(), this,
        [first = true, attempt_seen = false](PrinterSwitchFlow* self, int value) mutable {
            const auto state = static_cast<ConnectionState>(value);
            if (std::exchange(first, false)) {
                return;
            }
            if (state != ConnectionState::CONNECTED && state != ConnectionState::FAILED) {
                attempt_seen = true;
                return;
            }
            if (attempt_seen) {
                spdlog::info("[PrinterSwitchFlow] Connection state {} ends the switch card", value);
                self->dismiss_interstitial();
            }
        },
        get_printer_state().get_subjects_lifetime());
    lv_timer_t* timeout = lv_timer_create(
        [](lv_timer_t* timer) {
            auto* self = static_cast<PrinterSwitchFlow*>(lv_timer_get_user_data(timer));
            // One-shot: LVGL deletes the timer after this run.
            self->m_connect_timeout.release();
            spdlog::warn("[PrinterSwitchFlow] No connection after {} ms; dropping the switch card",
                         CONNECT_WAIT_MS);
            self->dismiss_interstitial();
        },
        CONNECT_WAIT_MS, this);
    lv_timer_set_repeat_count(timeout, 1);
    m_connect_timeout.reset(timeout);
}

void PrinterSwitchFlow::dismiss_interstitial() {
    m_connect_observer.reset();
    m_connect_timeout.reset();
    ui::safe_delete_deferred(m_interstitial);
}

bool PrinterSwitchFlow::confirm_pending() {
    if (!m_confirm_dialog) {
        return false;
    }
    // Read from the dialog itself, not a flag its callbacks clear: a dismissal reaches the
    // caller a tick late, after a newer confirmation may already be up, and some closes never
    // reach the callbacks at all.
    auto& stack = ModalStack::instance();
    lv_obj_t* backdrop = stack.backdrop_for(m_confirm_dialog);
    if (backdrop && !stack.is_exiting(backdrop)) {
        if (!lv_obj_has_flag(backdrop, LV_OBJ_FLAG_HIDDEN)) {
            return true;
        }
        // Hidden but still stacked, it would keep ModalStack non-empty and hold back
        // everything that waits for no modal to be open.
        spdlog::warn("[PrinterSwitchFlow] The switch confirmation was hidden; closing it");
        Modal::hide(m_confirm_dialog);
    }
    m_confirm_dialog = nullptr;
    return false;
}

bool PrinterSwitchFlow::request_switch(const std::string& printer_id) {
    if (m_soft_restart_in_progress || confirm_pending()) {
        spdlog::warn("[PrinterSwitchFlow] Ignoring switch to '{}': a switch is already running",
                     printer_id);
        return false;
    }
    if (printer_id == m_connected_printer_id) {
        // Picking the printer the panel is not connected to connects it; nothing is left
        // behind to ask about.
        return printer_connection_live() ? false : switch_printer(printer_id);
    }
    // A connected printer that is no longer in the list was removed, and its removal was
    // already confirmed; nothing is left to ask about.
    const auto ids = m_config->get_printer_ids();
    const bool connected_removed =
        std::find(ids.begin(), ids.end(), m_connected_printer_id) == ids.end();
    if (connected_removed || !active_printer_is_printing()) {
        return switch_printer(printer_id);
    }

    const std::string message = fmt::format(
        fmt::runtime(lv_tr("{} is still printing. The print keeps running after you switch.")),
        m_config->get_active_printer_name());
    ui::ConfirmOptions options;
    options.owner_token = m_async.token();
    m_confirm_dialog = ui::modal_confirm(
        lv_tr("Switch Printer"), message.c_str(), ModalSeverity::Warning, lv_tr("Switch Printer"),
        [this, printer_id] {
            m_confirm_dialog = nullptr;
            // Out of the dialog's click handler: the restart tears down the
            // screen the dialog sits on.
            m_async.defer("PrinterSwitchFlow::confirmed_switch",
                          [this, printer_id] { switch_printer(printer_id); });
        },
        options);
    return false;
}

bool PrinterSwitchFlow::switch_printer(const std::string& printer_id) {
    if (confirm_pending()) {
        spdlog::warn(
            "[PrinterSwitchFlow] Ignoring switch_printer while a switch is being confirmed");
        return false;
    }
    if (m_soft_restart_in_progress) {
        spdlog::warn("[PrinterSwitchFlow] Ignoring switch_printer during active soft restart");
        return false;
    }
    SoftRestartLatch soft_restart(m_soft_restart_in_progress);

    spdlog::info("[PrinterSwitchFlow] Switching to printer '{}'...", printer_id);

    const std::string previous_id = m_config->get_active_printer_id();
    if (!m_config->set_active_printer(printer_id)) {
        spdlog::error("[PrinterSwitchFlow] Failed to switch — unknown printer '{}'", printer_id);
        return false;
    }
    const BootCrashRecord replaced = record_switch_away(m_connected_printer_id, printer_id);
    // A switch the config does not remember would come back as the old printer after a
    // restart, so an unsaved switch does not happen.
    if (!save_or_report()) {
        m_config->set_active_printer(previous_id);
        restore_boot_crash_record(replaced);
        return false;
    }
    // A wizard left by a switch has nothing for a late cancel to restore.
    clear_wizard_previous_printer_id();

    // Per-printer state lives at /printers/<active>/… and is reached via Config::df().
    // The active printer just changed, so df() now points at the new printer — fire every
    // registered per-printer cache invalidator BEFORE teardown, while df() is already
    // correct, so nothing keeps serving the previous printer's values (#804).
    PrinterCacheRegistry::instance().invalidate_all();

    const std::string title =
        fmt::format(fmt::runtime(lv_tr("Switching to {}")), m_config->get_active_printer_name());
    paint_loading_card(title);
    m_restart.teardown();
    const bool connecting = m_restart.rebuild();

    m_restart.land_home();
    m_connected_printer_id = printer_id;
    await_connection(title, connecting);

    spdlog::info("[PrinterSwitchFlow] Switched to printer '{}'", printer_id);
    return true;
}

void PrinterSwitchFlow::add_printer_via_wizard() {
    if (m_soft_restart_in_progress) {
        spdlog::warn(
            "[PrinterSwitchFlow] Ignoring add_printer_via_wizard during active soft restart");
        return;
    }
    SoftRestartLatch soft_restart(m_soft_restart_in_progress);

    const std::string new_id = m_config->next_printer_id();
    std::string previous_id = m_config->get_active_printer_id();

    // Create empty printer entry with wizard_completed=false so is_wizard_required()
    // returns true (without this, root-level wizard_completed fallback blocks the wizard)
    nlohmann::json printer_data = {{"wizard_completed", false}};
    m_config->add_printer(new_id, printer_data);
    m_config->set_active_printer(new_id);
    const BootCrashRecord replaced = record_switch_away(m_connected_printer_id, new_id);
    if (!save_or_report()) {
        m_config->remove_printer(new_id);
        m_config->set_active_printer(previous_id);
        restore_boot_crash_record(replaced);
        return;
    }

    // Store previous ID so wizard cancellation can recover
    m_wizard_previous_printer_id = previous_id;
    m_wizard_replaced_record = replaced;

    spdlog::info("[PrinterSwitchFlow] Adding new printer '{}' via wizard (previous: '{}')", new_id,
                 previous_id);

    // Same active-printer change as switch_printer(): Config::df() has moved to the new
    // entry, so every per-printer cache must be dropped before teardown.
    PrinterCacheRegistry::instance().invalidate_all();

    // The rebuild runs the wizard itself when is_wizard_required() returns true (it does for
    // the new empty entry), so the wizard must not be launched again here.
    const std::string title = lv_tr("Adding printer");
    paint_loading_card(title);
    m_restart.teardown();

    // Registered after the teardown (which clears it) and before the rebuild (which runs the
    // wizard).
    set_wizard_cancel_callback([this]() { cancel_add_printer_wizard(); });

    const bool connecting = m_restart.rebuild();
    m_connected_printer_id = new_id;
    await_connection(title, connecting);
}

void PrinterSwitchFlow::cancel_add_printer_wizard() {
    if (m_soft_restart_in_progress) {
        spdlog::warn(
            "[PrinterSwitchFlow] Ignoring cancel_add_printer_wizard during active soft restart");
        return;
    }

    if (m_wizard_previous_printer_id.empty()) {
        spdlog::debug("[PrinterSwitchFlow] No add-printer recovery state — ignoring cancel");
        return;
    }

    std::string failed_id = m_config->get_active_printer_id();
    std::string restore_id = m_wizard_previous_printer_id;
    spdlog::info(
        "[PrinterSwitchFlow] Cancelling add-printer wizard — removing '{}', restoring '{}'",
        failed_id, restore_id);

    m_config->remove_printer(failed_id);
    m_config->set_active_printer(restore_id);
    // The abandoned entry is gone, so the fallback and crash run return to what they were
    // before the wizard. The hold stays cleared: starting the wizard was the user's pick,
    // and the restore below connects the restored printer in this session.
    BootCrashRecord restored = m_wizard_replaced_record;
    restored.connect_held = false;
    restore_boot_crash_record(restored);
    // Unsaved, the abandoned entry reappears after a restart; the restore still runs.
    save_or_report();
    clear_wizard_previous_printer_id();

    // Defer wizard teardown + soft restart — we're called from a wizard button click handler,
    // so the wizard_container must survive until the event callback returns.
    m_async.defer("PrinterSwitchFlow::cancel_add_printer_wizard", [this]() {
        SoftRestartLatch soft_restart(m_soft_restart_in_progress);

        set_wizard_active(false);
        ui_wizard_deinit_subjects();

        // set_active_printer() above restored the previous printer, so Config::df() moved
        // again — drop every per-printer cache before teardown.
        PrinterCacheRegistry::instance().invalidate_all();

        const std::string title = fmt::format(fmt::runtime(lv_tr("Switching to {}")),
                                              m_config->get_active_printer_name());
        paint_loading_card(title);
        m_restart.teardown();
        const bool connecting = m_restart.rebuild();
        m_restart.land_home();
        m_connected_printer_id = m_config->get_active_printer_id();
        await_connection(title, connecting);
    });
}

bool PrinterSwitchFlow::add_printer(const std::string& host, int port) {
    const std::string existing = m_config->find_printer_by_host(host, port);
    if (!existing.empty()) {
        spdlog::info("[PrinterSwitchFlow] {}:{} is already printer '{}'", host, port, existing);
        return request_switch(existing);
    }

    const std::string id = m_config->next_printer_id();
    m_config->add_printer(id, {{"moonraker_host", host}, {"moonraker_port", port}});
    spdlog::info("[PrinterSwitchFlow] Added printer '{}' at {}:{}", id, host, port);
    // Kept in the list unsaved rather than dropped, and not switched to.
    if (!save_or_report()) {
        return false;
    }
    return request_switch(id);
}

PrinterSwitchFlow::BootCrashRecord PrinterSwitchFlow::record_switch_away(const std::string& from_id,
                                                                         const std::string& to_id) {
    BootCrashRecord was{m_config->get<bool>(BOOT_CONNECT_HOLD_KEY, false),
                        m_config->get<std::string>(SWITCH_PREVIOUS_PRINTER_KEY, ""),
                        m_config->get<int>(BOOT_CRASH_STREAK_KEY, 0)};
    m_config->set<bool>(BOOT_CONNECT_HOLD_KEY, false);
    if (from_id != to_id) {
        m_config->set<std::string>(SWITCH_PREVIOUS_PRINTER_KEY, from_id);
        m_config->set<int>(BOOT_CRASH_STREAK_KEY, 0);
    }
    return was;
}

void PrinterSwitchFlow::restore_boot_crash_record(const BootCrashRecord& record) {
    m_config->set<bool>(BOOT_CONNECT_HOLD_KEY, record.connect_held);
    m_config->set<std::string>(SWITCH_PREVIOUS_PRINTER_KEY, record.previous_printer_id);
    m_config->set<int>(BOOT_CRASH_STREAK_KEY, record.crash_streak);
}

bool PrinterSwitchFlow::save_or_report() {
    if (m_config->save()) {
        return true;
    }
    spdlog::error("[PrinterSwitchFlow] Saving the printer list failed");
    ToastManager::instance().show(ToastSeverity::ERROR,
                                  lv_tr("Failed to save printer configuration"));
    return false;
}

} // namespace helix
