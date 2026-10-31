// Copyright (C) 2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "async_lifetime_guard.h"
#include "home_layout_share.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace helix {

class IMoonrakerAPI;
class PanelWidgetConfig;

/// Moves the home layout between a printer's own screen and remote clients
/// through Moonraker's database (home_layout::kNamespace / kKey).
///
/// The screen whose Moonraker runs on this host publishes: once per
/// connection and after every home-layout save. A screen talking to a remote
/// Moonraker fetches on connect and holds the result, tagged with the active
/// printer id, until the seed point takes it. Nothing is published or held
/// while the connection's locality is Unknown
/// (PrinterState::moonraker_locality()).
///
/// Main thread throughout. Database replies arrive on the Moonraker callback
/// thread; they are parsed there and handed back through the UI queue.
class HomeLayoutSync {
  public:
    using Layout = home_layout::ParsedLayout;

    static HomeLayoutSync& instance();

    HomeLayoutSync(const HomeLayoutSync&) = delete;
    HomeLayoutSync& operator=(const HomeLayoutSync&) = delete;

    /// Use @p api for every database call, install the PanelWidgetConfig
    /// save listener, and make adopt_late_seed() the seed listener.
    /// @p api must outlive the next shutdown().
    void init(IMoonrakerAPI& api);

    /// Forget the API and the listener, drop what is held, and discard every
    /// reply still in flight (their `done` callbacks never run). Called before
    /// the API is destroyed.
    void shutdown();

    /// The connection's CONNECTED edge, after its locality verdict is set.
    /// Local: publish the current home layout. Remote: fetch and hold it.
    void on_connected();

    /// Any other connection edge, and a printer switch: drop the held seed,
    /// ignore replies to requests made before now, and let the next
    /// connection publish again even if the layout is unchanged.
    void on_disconnected();

    /// The seed held for @p printer_id, once; nullopt when none is held for it.
    std::optional<Layout> take_seed(const std::string& printer_id);

    /// Fetch the published layout now, for the manual action. @p done runs on
    /// the main thread with the layout, or nullopt when there is none, it is
    /// unreadable, the request failed, or the active printer changed meanwhile.
    void fetch_now(std::function<void(std::optional<Layout>)> done);

    /// The populate-time seed point, for a home layout whose default anchors
    /// are about to be applied on a @p cols x @p rows grid: take the seed held
    /// for the active printer and import it into @p config, then toast. True
    /// on import; false, with @p config untouched, when @p config is not the
    /// home panel, no seed is held, or the import refuses it.
    bool seed_pending_default(PanelWidgetConfig& config, int cols, int rows);

    /// A seed held for @p printer_id after the home default was applied:
    /// import it while the layout is still an unedited default on a known
    /// grid, then rebuild home and toast. A layout still waiting for its
    /// anchors, or with no grid yet, leaves the seed for seed_pending_default().
    /// An edited layout, or one under an open edit session, drops it.
    void adopt_late_seed(const std::string& printer_id);

    /// Told on the main thread each time a fetched seed is held for the active
    /// printer, with that printer's id. The listener calls take_seed() to
    /// claim it. One listener; an empty function removes it.
    void set_seed_listener(std::function<void(const std::string& printer_id)> fn);

    /// Post @p config's layout when this connection is Local, it is the home
    /// panel, its grid is known, it is not an unedited default, and the
    /// payload (saved_at aside) differs from the last one posted on this
    /// connection. The save listener's body.
    void publish(const PanelWidgetConfig& config);

  private:
    HomeLayoutSync() = default;

    struct HeldSeed {
        std::string printer_id;
        Layout layout;
    };

    /// Import @p seed into @p config on a @p cols x @p rows grid, log and toast.
    /// False, with @p config untouched, when the import refuses it.
    bool import_seed(PanelWidgetConfig& config, const Layout& seed, int cols, int rows);

    using ReplyHandler = std::function<void(std::optional<Layout>)>;

    /// Request the published layout; @p on_reply runs on the main thread,
    /// only if the reply belongs to this connection and the active printer.
    void request(ReplyHandler on_reply, std::function<void()> on_stale);
    void hold(const std::string& printer_id, std::optional<Layout> layout);

    IMoonrakerAPI* api_ = nullptr;
    std::optional<HeldSeed> held_;
    std::function<void(const std::string&)> seed_listener_;
    /// Hash of the last payload posted on this connection, saved_at excluded.
    std::optional<std::size_t> last_posted_hash_;
    /// Bumped by on_disconnected(): a reply carrying an older value belongs
    /// to a connection that is gone.
    uint64_t connection_generation_ = 0;
    AsyncLifetimeGuard lifetime_;
};

} // namespace helix
