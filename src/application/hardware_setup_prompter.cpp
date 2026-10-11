// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file hardware_setup_prompter.cpp
 * @brief The prompts a discovery pass can raise about hardware: reconfig wizard,
 *        deferred-setup offer, printer-type mismatch warning.
 */

#include "hardware_setup_prompter.h"

#include "ui_modal.h"
#include "ui_wizard.h"

#include "app_globals.h"
#include "async_lifetime_guard.h"
#include "config.h"
#include "hardware_role_registry.h"
#include "i_moonraker_api.h"
#include "lvgl/src/others/translation/lv_translation.h"
#include "printer_detector.h"
#include "printer_discovery.h"
#include "printer_fan_state.h"
#include "printer_state.h"
#include "runtime_config.h"
#include "wizard_config_paths.h"
#include "wizard_step_logic.h"

#include <spdlog/fmt/fmt.h>
#include <spdlog/spdlog.h>

using namespace helix;

namespace helix {

HardwarePrompt choose_prompt(const HardwarePromptGates& g,
                             const std::function<bool()>& deferred_steps_empty) {
    // Every prompt waits for a changed hardware shape, an idle printer and no first-run
    // wizard: a wizard over a live print or a second wizard is never right.
    if (!g.hw_changed || g.print_active || g.wizard_blocked) {
        return HardwarePrompt::None;
    }
    // The three reasons are mutually exclusive, in this precedence, so one pass never
    // stacks two dialogs.
    if (g.reconfig_steps_pending) {
        return g.reconfig_shown ? HardwarePrompt::None : HardwarePrompt::Reconfig;
    }
    if (g.hardware_setup_deferred) {
        if (g.deferred_prompt_shown) {
            return HardwarePrompt::None;
        }
        return deferred_steps_empty() ? HardwarePrompt::DeferredSilent
                                      : HardwarePrompt::DeferredOffer;
    }
    return g.type_mismatch_shown ? HardwarePrompt::None : HardwarePrompt::TypeMismatch;
}

void HardwareSetupPrompter::reset_for_new_connection() {
    guards.reconfig_shown = false;
    guards.deferred_prompt_shown = false;
    guards.type_mismatch_shown = false;
    pending_steps_.clear();
}

HardwarePrompt HardwareSetupPrompter::run_discovery_prompts(const PrinterDiscovery& hw,
                                                            bool hw_changed, bool print_active,
                                                            bool hardware_setup_deferred) {
    // Route unresolved GUIDED hardware roles (a saved fan/heater role with no
    // confident live substitute) into the targeted reconfig wizard, only the
    // affected step(s). Skipped while the first-run wizard is required/active.
    // Unresolved steps are purely a function of (saved config, current hardware), so
    // an unchanged shape would only harass the user again.
    auto reconfig_steps = helix::unresolved_guided_steps(Config::get_instance(), hw);

    HardwarePromptGates gates;
    gates.hw_changed = hw_changed;
    gates.print_active = print_active;
    gates.wizard_blocked = Config::get_instance()->is_wizard_required() || is_wizard_active();
    gates.reconfig_steps_pending = !reconfig_steps.empty();
    gates.hardware_setup_deferred = hardware_setup_deferred;
    gates.reconfig_shown = guards.reconfig_shown;
    gates.deferred_prompt_shown = guards.deferred_prompt_shown;
    gates.type_mismatch_shown = guards.type_mismatch_shown;

    std::vector<wizard::StepId> deferred_steps;
    const HardwarePrompt prompt = choose_prompt(gates, [&deferred_steps] {
        deferred_steps = ui_wizard_deferred_hardware_steps();
        return deferred_steps.empty();
    });

    switch (prompt) {
    case HardwarePrompt::None:
        break;
    case HardwarePrompt::Reconfig:
        guards.reconfig_shown = true;
        launch_reconfig_wizard(std::move(reconfig_steps));
        break;
    case HardwarePrompt::DeferredOffer:
        guards.deferred_prompt_shown = true;
        prompt_deferred_hardware_setup(std::move(deferred_steps));
        break;
    case HardwarePrompt::DeferredSilent:
        // Nothing left to ask about (a preset already answers these, or the printer
        // has none of the optional hardware). Settle the debt silently rather than
        // showing a dead-end dialog.
        guards.deferred_prompt_shown = true;
        spdlog::info("[HardwareSetup] Deferred hardware setup has no steps to offer; "
                     "settling silently");
        settle_deferred_hardware_setup();
        break;
    case HardwarePrompt::TypeMismatch:
        // Saved printer type vs detected hardware: one actionable prompt per saved
        // type. maybe_warn_type_mismatch() applies its own further gates.
        maybe_warn_type_mismatch(hw);
        break;
    }
    return prompt;
}

void HardwareSetupPrompter::launch_reconfig_wizard(std::vector<wizard::StepId> steps) {
    IMoonrakerAPI* api = api_();
    if (!api) {
        return;
    }
    ui_wizard_register_event_callbacks();
    ui_wizard_container_register_responsive_constants();
    ui_wizard_init_subjects();
    // Ending the session — Cancel here, Finish in the on_complete callback
    // below — settles every guided role the session's steps could not
    // resolve: a step only offers controls for the roles it shows (the fan
    // step has no aux dropdown), so a preset-saved role with no live match
    // can never be satisfied inside the session and would relaunch the
    // wizard on every boot. settle_targeted_reconfig() writes "" (declined)
    // for each such role, read against the CURRENT discovered hardware.
    set_wizard_cancel_callback([api]() {
        // reapply_hardware_roles() is intentionally NOT called here:
        // ui_wizard_complete_targeted() fires the on_complete callback
        // (registered in ui_wizard_create_targeted below), which already
        // reapplies the roles. Calling it here too would be a redundant
        // double reapply.
        helix::settle_targeted_reconfig(Config::get_instance(), api->hardware());
        ui_wizard_complete_targeted();
        set_wizard_cancel_callback(nullptr);
    });
    ui_wizard_create_targeted(screen_(), std::move(steps), [this, api]() {
        set_wizard_cancel_callback(nullptr);
        helix::settle_targeted_reconfig(Config::get_instance(), api->hardware());
        reapply_hardware_roles();
    });
}

void HardwareSetupPrompter::reapply_hardware_roles() {
    lifetime_.defer("HardwareSetupPrompter::reapply_hardware_roles", [this]() {
        IMoonrakerAPI* api = api_();
        if (!api) {
            return;
        }
        const auto& fans = api->hardware().fans();
        const auto& heaters = api->hardware().heaters();
        // Re-resolve + persist fan roles, then rebind fan UI to the new mapping.
        // apply_roles, not init_fans: the wizard changed which fan plays which
        // role, not which fans exist, so the discovered list comes from what
        // discovery actually stored rather than being re-passed from here.
        auto roles = helix::FanRoleConfig::from_config(Config::get_instance(), fans);
        get_printer_state().fan_state().apply_roles(roles);
        // Heater roles persist back to config (no dedicated runtime fan-style consumer).
        helix::resolve_role_from_config(helix::HardwareRoleId::HotendHeater, Config::get_instance(),
                                        heaters, /*persist_autoheal=*/true);
        helix::resolve_role_from_config(helix::HardwareRoleId::BedHeater, Config::get_instance(),
                                        heaters, /*persist_autoheal=*/true);
    });
}

void HardwareSetupPrompter::settle_deferred_hardware_setup() {
    Config* config = Config::get_instance();
    if (!helix::wizard_clear_hardware_setup_deferred(config)) {
        return;
    }
    if (!config->save()) {
        spdlog::warn("[HardwareSetup] Failed to persist deferred hardware setup decision");
    }
}

void HardwareSetupPrompter::launch_deferred_hardware_setup() {
    auto steps = std::move(pending_steps_);
    pending_steps_.clear();
    if (steps.empty()) {
        return;
    }
    spdlog::info("[HardwareSetup] Launching deferred hardware setup ({} step(s))", steps.size());

    ui_wizard_register_event_callbacks();
    ui_wizard_container_register_responsive_constants();
    ui_wizard_init_subjects();
    // Back on the first targeted step has nothing to retreat to, so give it the
    // same dismiss semantics as the reconfig wizard.
    set_wizard_cancel_callback([]() {
        ui_wizard_complete_targeted();
        set_wizard_cancel_callback(nullptr);
    });
    HardwareSetupPrompter* app = this;
    ui_wizard_create_targeted(screen_(), std::move(steps), [app]() {
        set_wizard_cancel_callback(nullptr);
        // ui_wizard_complete_targeted() deliberately skips the expected-hardware
        // population, so record the user's fresh picks here.
        ui_wizard_record_expected_hardware(Config::get_instance());
        app->reapply_hardware_roles();
    });
}

void HardwareSetupPrompter::prompt_deferred_hardware_setup(
    std::vector<helix::wizard::StepId> steps) {
    // The steps to run are parked on the prompter for the confirm
    // callback's timer to consume (launch_deferred_hardware_setup() reads them
    // from there). on_dismiss clears them too, so a backdrop tap or ESC cannot
    // strand the offer.
    pending_steps_ = std::move(steps);
    spdlog::info("[HardwareSetup] Offering deferred hardware setup ({} step(s))",
                 pending_steps_.size());

    helix::ui::ConfirmOptions opts;
    opts.on_cancel = [this] {
        pending_steps_.clear();
        // Declining is final for this printer. The offer is for optional
        // role assignments the app already has working defaults for, the
        // snapshot was written regardless so nothing is flagged either way,
        // and re-asking on every boot is the exact nag the surrounding
        // reconfig-wizard code records declines to avoid. `--wizard` still
        // re-runs setup, and a saved role that later breaks still routes to
        // the targeted reconfig wizard on its own.
        settle_deferred_hardware_setup();
        spdlog::info("[HardwareSetup] Deferred hardware setup declined");
    };
    opts.cancel_text = lv_tr("Not now");
    opts.on_dismiss = [this] { pending_steps_.clear(); };
    opts.owner_token = lifetime_.token();

    helix::ui::modal_confirm(
        lv_tr("Printer hardware detected"),
        lv_tr("Your printer was offline during setup, so hardware options were skipped. "
              "Set them up now?"),
        ModalSeverity::Info, lv_tr("Set up"),
        [this] {
            // Settle first: the wizard tears itself down asynchronously, and a
            // crash mid-run must not leave the offer pending forever.
            settle_deferred_hardware_setup();
            // Build the wizard AFTER the modal's exit animation, not inside the
            // click that started it: the dialog's own close only marks the
            // backdrop exiting, so creating the full-screen wizard here would
            // put it underneath a still-fading backdrop. The pending step list
            // lives on the prompter until the timer consumes it.
            lv_timer_t* launch = lv_timer_create(
                [](lv_timer_t* t) {
                    auto* self = static_cast<HardwareSetupPrompter*>(lv_timer_get_user_data(t));
                    lv_timer_delete(t);
                    self->launch_deferred_hardware_setup();
                },
                300, this);
            lv_timer_set_repeat_count(launch, 1);
        },
        opts);
}

void HardwareSetupPrompter::settle_type_mismatch_warning() {
    auto* cfg = Config::get_instance();
    cfg->set<std::string>(cfg->df() + helix::wizard::TYPE_MISMATCH_SHOWN_FOR,
                          cfg->get<std::string>(cfg->df() + helix::wizard::PRINTER_TYPE, ""));
    if (!cfg->save()) {
        spdlog::warn("[HardwareSetup] Failed to persist type mismatch decision");
    }
}

void HardwareSetupPrompter::maybe_warn_type_mismatch(const helix::PrinterDiscovery& hardware) {
    // The gates below all decline silently in normal operation. A debug bundle
    // is the only view we get of a reporter's run, so each one says why it
    // declined: without that there is no way to tell a 68%-confidence near-miss
    // apart from detection returning nothing at all (bundle TZT85MQ3).
    if (guards.type_mismatch_shown) {
        spdlog::debug("[HardwareSetup] Type mismatch check skipped: already prompted this session");
        return;
    }
    if (get_runtime_config()->should_mock_moonraker()) {
        // A mock printer's identity is whatever the persona declares, so a
        // mismatch against the saved type says nothing about real hardware.
        // Gate on the runtime-config predicate, not on HELIX_MOCK_PRINTER:
        // plain --test runs the mock client without that env var ever being
        // set, so the old getenv check let every --test launch open the
        // prompt against whatever type settings-test.json happened to carry.
        spdlog::debug("[HardwareSetup] Type mismatch check skipped: mock printer");
        return;
    }
    if (Config::get_instance()->is_wizard_required() || is_wizard_active()) {
        spdlog::debug("[HardwareSetup] Type mismatch check skipped: wizard required or active");
        return;
    }

    auto* cfg = Config::get_instance();
    const std::string stored = cfg->get<std::string>(cfg->df() + helix::wizard::PRINTER_TYPE, "");

    // The saved type is a display name, so an entry renamed in the printer
    // database orphans every config written under the old one and detection
    // then contradicts a type that was never wrong. Resolve through the
    // database's alias list and heal the stored value, otherwise the stale
    // name keeps missing every other name-keyed lookup too (image, preset,
    // pre-print profile) long after this prompt is dismissed.
    const std::string saved = PrinterDetector::canonical_type_name(stored);
    if (saved != stored) {
        cfg->set<std::string>(cfg->df() + helix::wizard::PRINTER_TYPE, saved);
        if (!cfg->save()) {
            spdlog::warn("[HardwareSetup] Failed to persist renamed printer type '{}' -> '{}'",
                         stored, saved);
        }
    }

    // Canonicalised too: a dismissal recorded under the pre-rename name still
    // answers for the same printer.
    const std::string flag = PrinterDetector::canonical_type_name(
        cfg->get<std::string>(cfg->df() + helix::wizard::TYPE_MISMATCH_SHOWN_FOR, ""));

    auto detected = PrinterDetector::auto_detect(hardware);
    const auto decision = PrinterDetector::classify_type_mismatch(saved, detected, flag);
    if (decision != PrinterDetector::MismatchDecision::Warn) {
        // info, not debug: this runs once per discovery pass, and it is the line
        // that answers "why was there no prompt?" in a bundle.
        spdlog::info(
            "[HardwareSetup] No type mismatch prompt: detected '{}' at {}% (runner-up '{}' "
            "at {}%, margin {}, {} tied), saved '{}', dismissed-for '{}', need >={}% and "
            "margin >={} - {}",
            detected.type_name, detected.confidence, detected.runner_up_type_name,
            detected.runner_up_confidence, detected.margin(), detected.tied_count, saved, flag,
            PrinterDetector::MISMATCH_MIN_CONFIDENCE, PrinterDetector::DETECT_MIN_MARGIN,
            PrinterDetector::mismatch_decision_name(decision));
        return;
    }

    // Session guard: one prompt per boot regardless of which button dismisses it.
    guards.type_mismatch_shown = true;
    spdlog::info("[HardwareSetup] Printer type mismatch: saved '{}' but detected '{}' ({}%)", saved,
                 detected.type_name, detected.confidence);

    // modal_confirm takes a plain const char* - compose the parameterized body
    // first (fmt::runtime: the format string is the translated handle, not a
    // compile-time literal).
    const std::string body =
        fmt::format(fmt::runtime(lv_tr("This printer looks like a {} ({}% confidence), but it is "
                                       "set up as a {}. A wrong type applies incorrect pre-print "
                                       "options and presets.")),
                    detected.type_name, detected.confidence, saved);

    helix::ui::ConfirmOptions opts;
    opts.on_cancel = [this] {
        // Declining is final for this saved type. Keeping the type is a
        // deliberate choice (a heavily modified printer can legitimately
        // outvote a heuristic), and the persisted flag stops the
        // prompt from re-appearing every boot. The model picker remains
        // available from Printer Manager and the full `--wizard` run.
        settle_type_mismatch_warning();
        spdlog::info("[HardwareSetup] Type mismatch warning declined");
    };
    opts.cancel_text = lv_tr("Keep current");
    opts.owner_token = lifetime_.token();
    // No on_dismiss, deliberately: a backdrop tap or ESC is not an answer, so
    // the prompt stays armed for the next boot. Only a button settles it, and
    // an accidental tap must not permanently silence a wrong-printer warning.

    helix::ui::modal_confirm(
        lv_tr("Printer type mismatch"), body.c_str(), ModalSeverity::Warning, lv_tr("Choose Model"),
        [this] {
            // Settle first: the wizard tears itself down asynchronously, and a
            // crash mid-run must not leave the prompt pending forever.
            settle_type_mismatch_warning();
            // Build the wizard AFTER the modal's exit animation, not inside the
            // click that started it: the dialog's own close only marks the
            // backdrop exiting, so creating the full-screen wizard here would
            // put it underneath a still-fading backdrop (same 300 ms one-shot
            // as launch_deferred_hardware_setup).
            lv_timer_t* launch = lv_timer_create(
                [](lv_timer_t* t) {
                    auto* self = static_cast<HardwareSetupPrompter*>(lv_timer_get_user_data(t));
                    lv_timer_delete(t);
                    self->launch_type_reidentify_wizard();
                },
                300, this);
            lv_timer_set_repeat_count(launch, 1);
        },
        opts);
}

void HardwareSetupPrompter::launch_type_reidentify_wizard() {
    spdlog::info("[HardwareSetup] Launching printer re-identify wizard");
    ui_wizard_register_event_callbacks();
    ui_wizard_container_register_responsive_constants();
    ui_wizard_init_subjects();
    // Back on the first targeted step has nothing to retreat to, so give it the
    // same dismiss semantics as the deferred hardware-setup session.
    set_wizard_cancel_callback([]() {
        ui_wizard_complete_targeted();
        set_wizard_cancel_callback(nullptr);
    });
    HardwareSetupPrompter* app = this;
    ui_wizard_create_targeted(screen_(), {helix::wizard::StepId::PrinterIdentify}, [app]() {
        set_wizard_cancel_callback(nullptr);
        // The identify step's cleanup already persisted PRINTER_TYPE and applied
        // the new preset (ui_wizard_printer_identify.cpp cleanup). The preset
        // rewrote fan/heater role keys, so rebind the runtime mappings the same
        // way the deferred hardware-setup session does.
        app->reapply_hardware_roles();
    });
}

} // namespace helix
