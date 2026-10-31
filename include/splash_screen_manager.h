// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file splash_screen_manager.h
 * @brief Splash screen lifecycle management
 *
 * Coordinates splash screen dismissal with printer discovery timing.
 * Handles signaling the splash process and scheduling post-splash refreshes.
 */

#pragma once

#include <chrono>
#include <cstdint>
#include <sys/types.h>

namespace helix::application {

/**
 * @brief Manages splash screen lifecycle and discovery coordination
 *
 * The splash screen should stay visible until either:
 * 1. Printer discovery completes (preferred)
 * 2. Timeout expires (fallback)
 *
 * After splash exits, schedules display refresh to repaint the UI.
 */
class SplashScreenManager {
  public:
    /// Discovery timeout in milliseconds (how long to wait before giving up)
    static constexpr int64_t kDiscoveryTimeoutMs = 8000;

    /**
     * @brief Initialize with splash process PID
     *
     * @param splash_pid Process ID of splash screen (0 or negative = no splash)
     */
    void start(pid_t splash_pid);

    /**
     * @brief Mark printer discovery as complete
     *
     * Call this when Moonraker discovery finishes. This allows the splash
     * to exit before the timeout if discovery is fast.
     */
    void on_discovery_complete();

    /**
     * @brief Check conditions and signal splash to exit if ready
     *
     * Call once per frame. Signals the splash process when:
     * - Discovery is complete, OR
     * - Timeout has elapsed
     *
     * After signaling, waits briefly for the process to exit.
     */
    void check_and_signal();

    /**
     * @brief Check if splash has been signaled to exit
     */
    bool has_exited() const {
        return m_signaled;
    }

    /**
     * @brief True on the frame the next check_and_signal() would actually signal.
     *
     * Side-effect-free mirror of check_and_signal()'s decision: not yet signaled
     * AND (no splash PID, discovery complete, or the discovery timeout elapsed).
     * Used to force a full fb0-mirror repaint at handoff BEFORE the early splash
     * is signaled to exit, so the remote screen shows the real UI, not black.
     * Returns false once signaled, so a handoff repaint fires exactly once.
     */
    bool ready_to_signal() const {
        if (m_signaled) {
            return false;
        }
        if (m_splash_pid <= 0 || m_discovery_complete) {
            return true;
        }
        return elapsed_ms() >= kDiscoveryTimeoutMs;
    }

    /**
     * @brief Check if discovery has completed
     */
    bool is_discovery_complete() const {
        return m_discovery_complete;
    }

    /**
     * @brief Get elapsed time since start() was called
     */
    int64_t elapsed_ms() const;

    /**
     * @brief Check if post-splash screen refresh is needed
     *
     * After splash exits, the framebuffer needs to be repainted.
     */
    bool needs_post_splash_refresh() const {
        return m_post_refresh_frames > 0;
    }

    /**
     * @brief Mark one refresh frame as done
     *
     * Call after performing a full screen refresh.
     */
    void mark_refresh_done();

  private:
    /// Signal the splash process and wait for it to exit
    void signal_and_wait();

    pid_t m_splash_pid{0};
    bool m_signaled{false};
    bool m_discovery_complete{false};
    int m_post_refresh_frames{0};
    std::chrono::steady_clock::time_point m_start_time{std::chrono::steady_clock::now()};
};

} // namespace helix::application
