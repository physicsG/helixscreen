// Copyright (C) 2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "home_layout_sync.h"

#include "ui_toast_manager.h"

#include "app_globals.h"
#include "config.h"
#include "helix_psram_attr.h"
#include "helix_version.h"
#include "i_moonraker_api.h"
#include "json_utils.h"
#include "moonraker_error.h"
#include "panel_widget_config.h"
#include "panel_widget_manager.h"
#include "printer_state.h"

#include <spdlog/spdlog.h>

#include <ctime>
#include <utility>

namespace helix {

namespace {

constexpr const char* kHomePanelId = "home";

std::string active_printer_id() {
    Config* config = Config::get_instance();
    return config ? config->get_active_printer_id() : std::string();
}

/// Identity of a payload for "did this change": everything but the save time.
std::size_t payload_hash(nlohmann::json payload) {
    payload.erase("saved_at");
    return std::hash<std::string>{}(json_util::safe_dump(payload));
}

void show_seeded_toast() {
    ToastManager::instance().show(ToastSeverity::INFO,
                                  lv_tr("Home layout copied from the printer's screen"), 3000);
}

} // namespace

HomeLayoutSync& HomeLayoutSync::instance() {
    // PSRAM: internal DRAM statics shrink the heap the ESP32 WiFi driver starts in.
    static HELIX_PSRAM_BSS HomeLayoutSync sync;
    return sync;
}

void HomeLayoutSync::init(IMoonrakerAPI& api) {
    api_ = &api;
    PanelWidgetConfig::set_save_listener(
        [](const PanelWidgetConfig& config) { HomeLayoutSync::instance().publish(config); });
    seed_listener_ = [this](const std::string& printer_id) { adopt_late_seed(printer_id); };
}

void HomeLayoutSync::shutdown() {
    PanelWidgetConfig::set_save_listener(nullptr);
    lifetime_.invalidate();
    api_ = nullptr;
    held_.reset();
    last_posted_hash_.reset();
    ++connection_generation_;
}

void HomeLayoutSync::on_connected() {
    switch (get_printer_state().moonraker_locality()) {
    case MoonrakerLocality::Local:
        publish(PanelWidgetManager::instance().get_widget_config(kHomePanelId));
        break;
    case MoonrakerLocality::Remote: {
        const std::string printer_id = active_printer_id();
        request([this,
                 printer_id](std::optional<Layout> layout) { hold(printer_id, std::move(layout)); },
                nullptr);
        break;
    }
    case MoonrakerLocality::Unknown:
        break;
    }
}

void HomeLayoutSync::on_disconnected() {
    held_.reset();
    last_posted_hash_.reset();
    ++connection_generation_;
}

std::optional<HomeLayoutSync::Layout> HomeLayoutSync::take_seed(const std::string& printer_id) {
    if (!held_ || held_->printer_id != printer_id) {
        return std::nullopt;
    }
    std::optional<Layout> out = std::move(held_->layout);
    held_.reset();
    return out;
}

void HomeLayoutSync::fetch_now(std::function<void(std::optional<Layout>)> done) {
    if (!api_) {
        if (done) {
            done(std::nullopt);
        }
        return;
    }
    request(
        [done](std::optional<Layout> layout) {
            if (done) {
                done(std::move(layout));
            }
        },
        [done]() {
            if (done) {
                done(std::nullopt);
            }
        });
}

bool HomeLayoutSync::import_seed(PanelWidgetConfig& config, const Layout& seed, int cols,
                                 int rows) {
    if (!config.import_layout(seed, cols, rows)) {
        spdlog::info("[HomeLayoutSync] The printer's home layout did not import; keeping the "
                     "default layout");
        return false;
    }
    spdlog::info("[HomeLayoutSync] Seeded the home layout from the printer's screen ({}x{} onto "
                 "{}x{})",
                 seed.cols, seed.rows, cols, rows);
    show_seeded_toast();
    return true;
}

bool HomeLayoutSync::seed_pending_default(PanelWidgetConfig& config, int cols, int rows) {
    if (config.panel_id() != kHomePanelId) {
        return false;
    }
    std::optional<Layout> seed = take_seed(active_printer_id());
    if (!seed) {
        return false;
    }
    if (config.had_saved_layout()) {
        spdlog::debug("[HomeLayoutSync] Home layout was saved before; not seeding it");
        return false;
    }
    return import_seed(config, *seed, cols, rows);
}

void HomeLayoutSync::adopt_late_seed(const std::string& printer_id) {
    auto& manager = PanelWidgetManager::instance();
    PanelWidgetConfig& config = manager.get_widget_config(kHomePanelId);
    const auto [cols, rows] = config.stamped_grid();
    if (config.has_pending_anchors() || cols <= 0 || rows <= 0) {
        spdlog::debug("[HomeLayoutSync] Home default not applied yet; keeping the seed for the "
                      "first READY populate");
        return;
    }
    // Taken from here on: each remaining outcome is final for this seed.
    std::optional<Layout> seed = take_seed(printer_id);
    if (!seed) {
        return;
    }
    if (config.edit_session_open()) {
        spdlog::debug("[HomeLayoutSync] Home is being edited; not replacing it with the printer's "
                      "layout");
        return;
    }
    if (config.had_saved_layout() || !config.is_unedited_default()) {
        spdlog::debug("[HomeLayoutSync] Home layout is the user's own; ignoring the printer's");
        return;
    }
    if (import_seed(config, *seed, cols, rows)) {
        manager.notify_config_changed(kHomePanelId);
    }
}

void HomeLayoutSync::set_seed_listener(std::function<void(const std::string&)> fn) {
    seed_listener_ = std::move(fn);
}

void HomeLayoutSync::publish(const PanelWidgetConfig& config) {
    if (!api_ || config.panel_id() != kHomePanelId ||
        get_printer_state().moonraker_locality() != MoonrakerLocality::Local) {
        return;
    }
    if (config.grid_signature().empty()) {
        spdlog::debug("[HomeLayoutSync] Home layout has no grid yet; not publishing");
        return;
    }
    // A default nobody arranged is not the printer's layout: a client seeded
    // from it would announce a copy of something no one chose.
    if (config.is_unedited_default() || config.has_pending_anchors()) {
        spdlog::debug("[HomeLayoutSync] Home layout is an unedited default; not publishing");
        return;
    }

    nlohmann::json payload =
        home_layout::build_payload(config.published_pages(), config.grid_signature(),
                                   helix_version(), static_cast<int64_t>(std::time(nullptr)));
    const std::size_t hash = payload_hash(payload);
    if (last_posted_hash_ == hash) {
        return;
    }
    last_posted_hash_ = hash;

    spdlog::debug("[HomeLayoutSync] Publishing home layout ({})", config.grid_signature());
    api_->database_post_item(
        home_layout::kNamespace, home_layout::kKey, payload,
        []() { spdlog::debug("[HomeLayoutSync] Home layout published"); },
        lifetime_.bg_cb("HomeLayoutSync::publish_failed", [this, hash](const MoonrakerError& err) {
            spdlog::warn("[HomeLayoutSync] Publishing the home layout failed: {}", err.message);
            // The next save retries instead of matching a payload that never landed.
            if (last_posted_hash_ == hash) {
                last_posted_hash_.reset();
            }
        }));
}

void HomeLayoutSync::request(ReplyHandler on_reply, std::function<void()> on_stale) {
    if (!api_) {
        return;
    }
    const std::string printer_id = active_printer_id();
    const uint64_t generation = connection_generation_;

    // Main thread: the reply is for this connection and this printer, or it is
    // stale and only on_stale hears about it.
    auto deliver = [this, printer_id, generation, on_reply = std::move(on_reply),
                    on_stale = std::move(on_stale)](std::optional<Layout> layout) {
        if (generation != connection_generation_ || printer_id != active_printer_id()) {
            spdlog::debug("[HomeLayoutSync] Dropping a home layout reply for '{}': the "
                          "connection or active printer changed",
                          printer_id);
            if (on_stale) {
                on_stale();
            }
            return;
        }
        if (on_reply) {
            on_reply(std::move(layout));
        }
    };

    auto tok = lifetime_.token();
    api_->database_get_item(
        home_layout::kNamespace, home_layout::kKey,
        [tok, deliver](const nlohmann::json& value) mutable {
            // Callback thread: hand the raw json across. Parsing consults the
            // widget registry, which only the main thread may touch.
            tok.defer("HomeLayoutSync::reply", [deliver, value]() mutable {
                std::optional<Layout> layout = home_layout::parse_payload(value);
                if (!layout) {
                    spdlog::debug("[HomeLayoutSync] Published home layout is "
                                  "unreadable; ignoring");
                }
                deliver(std::move(layout));
            });
        },
        [tok, deliver](const MoonrakerError& err) mutable {
            // A missing key is the normal "nothing published" answer.
            spdlog::debug("[HomeLayoutSync] No published home layout ({})", err.message);
            tok.defer("HomeLayoutSync::reply_error",
                      [deliver]() mutable { deliver(std::nullopt); });
        });
}

void HomeLayoutSync::hold(const std::string& printer_id, std::optional<Layout> layout) {
    if (!layout) {
        return;
    }
    spdlog::info("[HomeLayoutSync] Holding the printer's home layout ({}x{}) for '{}'",
                 layout->cols, layout->rows, printer_id);
    held_ = HeldSeed{printer_id, std::move(*layout)};
    if (seed_listener_) {
        seed_listener_(printer_id);
    }
}

} // namespace helix
