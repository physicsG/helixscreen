// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace helix {

/**
 * @brief Decides when to move the station off an access point whose link keeps
 * stalling, on an SSID that several access points serve.
 *
 * A Moonraker connection that drops after a long silence points at the path
 * through the current access point, not at Moonraker: the association stays up
 * while nothing gets through. After LINK_STALL_DROPS such drops on one access
 * point, the caller scans and pick_alternative() names the strongest other
 * access point for the SSID. The abandoned one is skipped for AVOID_MS only, so
 * an access point that recovers is used again. With no alternative the station
 * stays where it is and keeps reconnecting.
 *
 * Pure: the WiFi backend feeds it events and applies its answers.
 */
class WifiApSteering {
  public:
    using Bssid = std::array<uint8_t, 6>;

    struct Candidate {
        Bssid bssid{};
        int rssi = 0;
    };

    /// A drop counts against the access point when nothing arrived for this long before it.
    static constexpr int64_t LINK_STALL_SILENCE_MS = 15'000;
    /// Stalled drops on one access point before leaving it.
    static constexpr int LINK_STALL_DROPS = 3;
    /// The drops must fall within this window; an older first drop starts the count again.
    static constexpr int64_t LINK_STALL_WINDOW_MS = 5 * 60'000;
    /// How long an abandoned access point is skipped.
    static constexpr int64_t AVOID_MS = 30 * 60'000;
    /// An alternative weaker than this is not worth moving to.
    static constexpr int MIN_RSSI = -85;
    /// An alternative may be at most this much weaker than the current access
    /// point. Stalls on the printer's side count too, so this keeps a run of
    /// printer restarts from moving a strong link to a marginal one.
    static constexpr int MAX_RSSI_LOSS_DB = 10;
    /// A steering scan that has not completed within this long is abandoned.
    static constexpr int64_t SCAN_REQUEST_TTL_MS = 30'000;

    void on_associated(const Bssid& bssid);

    /// @return true when the current access point should be abandoned: scan and
    ///         call pick_alternative() with what the scan saw.
    bool on_link_drop(int64_t now_ms, int64_t silence_ms);

    /// The scan on_link_drop() asked for has started.
    void scan_started(int64_t now_ms);

    /// @return true when a completed scan answers a pending steering request,
    ///         which it then clears. Any other scan steers nothing.
    bool take_scan_request(int64_t now_ms);

    /// Marks the current access point avoided and returns the strongest other
    /// candidate that is not avoided, clears MIN_RSSI and is within
    /// MAX_RSSI_LOSS_DB of the current access point when the scan saw it;
    /// nullopt means stay.
    std::optional<Bssid> pick_alternative(const std::vector<Candidate>& seen, int64_t now_ms);

    int stalled_drops() const {
        return stalled_drops_;
    }
    const Bssid& current() const {
        return current_;
    }

  private:
    struct Avoided {
        Bssid bssid{};
        int64_t until_ms = 0;
    };

    Bssid current_{};
    bool associated_ = false;
    int stalled_drops_ = 0;
    int64_t first_drop_ms_ = 0;
    std::optional<int64_t> scan_requested_ms_;
    std::vector<Avoided> avoided_;
};

/// "aa:bb:cc:dd:ee:ff"
std::string format_bssid(const WifiApSteering::Bssid& bssid);

} // namespace helix
