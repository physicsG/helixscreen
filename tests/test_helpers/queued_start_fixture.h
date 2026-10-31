// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "helix_shared_callback.h"

/**
 * @file queued_start_fixture.h
 * @brief The print-select panel over a mock queue and option store, for the
 *        suites that start queued jobs (print-select's two-tap path and the
 *        home tile's QueuedJobStarter).
 */

#include "../ui_test_utils.h"
#include "app_globals.h"
#include "i_moonraker_sub_apis.h"
#include "moonraker_api.h"
#include "moonraker_client_mock.h"
#include "moonraker_client_mock_test_access.h"
#include "print_select_panel_fixture.h"
#include "print_select_panel_test_access.h"
#include "print_start_grace.h"
#include "print_state_test_drivers.h"
#include "printer_state.h"
#include "printer_state_test_access.h"
#include "queued_job_options.h"
#include "scoped_macros_read.h"
#include "test_helpers/pre_print_option_sets.h"

#include <map>
#include <string>
#include <vector>

#include "../catch_amalgamated.hpp"

namespace helix::test {

/// Collects the user-facing warning (or info) toasts: toasts are compiled out of the
/// test build, so the hook is the only observable.
struct NotificationLog {
    enum class Kind { Warning, Info };
    std::vector<std::string> messages;

    explicit NotificationLog(Kind kind = Kind::Warning) : kind_(kind) {
        auto record = [this](const std::string& message) { messages.push_back(message); };
        if (kind_ == Kind::Info) {
            helix::ui::set_test_notification_info_hook(record);
        } else {
            helix::ui::set_test_notification_warning_hook(record);
        }
    }
    ~NotificationLog() {
        if (kind_ == Kind::Info) {
            helix::ui::set_test_notification_info_hook(nullptr);
        } else {
            helix::ui::set_test_notification_warning_hook(nullptr);
        }
    }
    NotificationLog(const NotificationLog&) = delete;
    NotificationLog& operator=(const NotificationLog&) = delete;

    bool contains(const std::string& needle) const {
        for (const auto& m : messages) {
            if (m.find(needle) != std::string::npos) {
                return true;
            }
        }
        return false;
    }

  private:
    Kind kind_;
};

using WarningLog = NotificationLog;

class QueuedStartFixture : private helix::PrintSelectGlobalStateReset,
                           public helix::PrintSelectPanelFixture {
  public:
    explicit QueuedStartFixture(PrintSelectApi api = PrintSelectApi::Real)
        : helix::PrintSelectPanelFixture(helix::PrintSelectFilelistHandler::Unregistered,
                                         helix::PrintSelectVisit::Immediate, api) {
        PrinterStateTestAccess::set_option_set(get_printer_state(), make_skip_and_addon_set());
        previous_api_ = get_moonraker_api();
        set_moonraker_api(api_.get());
        drain();
    }

    ~QueuedStartFixture() override {
        // With the panel and its api still alive: settle the state, then hand
        // the global api pointer back before the bases tear anything down.
        auto& ps = get_printer_state();
        if (ps.print_state().has_preparing_job()) {
            ps.print_state().retire_preparing(helix::PreparingExit::Superseded);
        }
        set_wire_state(ps, PrintJobState::STANDBY);
        drain();
        set_moonraker_api(previous_api_);
    }

    /// Writes a stored-options entry the way add_to_queue does: one post to
    /// the job's own key. The mock dispatches inline, so it answers before
    /// returning.
    void seed_store(const std::string& job_id, const std::string& filename,
                    std::map<std::string, bool> options) {
        api_->database_post_item(
            helix::queue::kOptionsDbNamespace, helix::queue::queued_job_option_key(job_id),
            helix::queue::encode_queued_job_entry(
                helix::queue::QueuedJobOptions{filename, std::move(options)}),
            []() {}, [](const MoonrakerError&) { FAIL("seed post failed"); });
    }

    /// Reads the stored option map straight from the (mock) database.
    helix::queue::QueuedJobOptionsMap read_store() {
        helix::queue::QueuedJobOptionsMap out;
        bool answered = false;
        api_->database_get_item(
            helix::queue::kOptionsDbNamespace, helix::queue::kOptionsDbKey,
            [&out, &answered](const json& value) {
                out = helix::queue::decode_queued_job_options(value);
                answered = true;
            },
            [&answered](const MoonrakerError&) { answered = true; });
        REQUIRE(answered);
        return out;
    }

    /// Whether the job is still in Moonraker's queue (the mock answers
    /// synchronously, so a capturing lambda is safe here).
    bool queue_has(const std::string& job_id) {
        bool found = false;
        api_->queue().get_queue_status(
            [&found, &job_id](const JobQueueStatus& status) {
                for (const auto& job : status.queued_jobs) {
                    if (job.job_id == job_id) {
                        found = true;
                    }
                }
            },
            [](const MoonrakerError& err) { FAIL(err.message); });
        return found;
    }

    static JobQueueEntry entry(const std::string& job_id, const std::string& filename) {
        return JobQueueEntry{job_id, filename, 0.0, 0.0};
    }

    const std::string* pending_job_id() const {
        return ::PrintSelectPanelTestAccess::pending_queued_job_id(*panel_);
    }

  private:
    IMoonrakerAPI* previous_api_ = nullptr;
};

/// QueuedStartFixture over the mock API with everything a queued start needs to reach
/// Moonraker: the print-start grace waited out, Klippy ready, the printer macros read,
/// and a printer.print.start the mock answers at once with no simulated print behind it.
class QueuedStartMockedFixture : public QueuedStartFixture {
  public:
    QueuedStartMockedFixture() : QueuedStartFixture(helix::PrintSelectApi::Mock) {
        helix::test::wait_out_print_start_grace();
        // The pre-start G-code is refused while Klipper reads as halted.
        prior_klippy_ = static_cast<helix::KlippyState>(
            lv_subject_get_int(get_printer_state().network_state().get_klippy_state_subject()));
        get_printer_state().set_klippy_state_sync(helix::KlippyState::READY);
        helix::MoonrakerClientMockTestAccess::set_method_handler(
            mock_client_, "printer.print.start",
            [](MoonrakerClientMock*, const json&,
               helix::SharedCallback<void(const json&)> success_cb,
               helix::SharedCallback<void(const MoonrakerError&)>) -> bool {
                success_cb(json{{"result", "ok"}});
                return true;
            });
    }

    ~QueuedStartMockedFixture() override {
        get_printer_state().set_klippy_state_sync(prior_klippy_);
        settle();
    }

    static void settle() {
        for (int i = 0; i < 6; ++i) {
            drain();
        }
    }

    int starts() const {
        return mock_client_.call_count("printer.print.start");
    }

    helix::test::ScopedMacrosRead macros;

  private:
    helix::KlippyState prior_klippy_ = helix::KlippyState::SHUTDOWN;
};

} // namespace helix::test
