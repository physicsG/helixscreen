// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/**
 * @file pending_startup_warnings.h
 * @brief Deferred user-visible warnings queued during pre-UI initialization.
 *
 * Display backends (DRM, fbdev) may need to report problems (e.g. "requested
 * resolution not available") at a point in startup where the toast/notification
 * system is not yet initialized. This singleton buffers those warnings so they
 * can be presented as toasts once the UI comes up.
 *
 * Thread-safety: enqueue() is safe to call from any thread. drain() must be
 * called on the main/UI thread.
 */

#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace helix {

class PendingStartupWarnings {
  public:
    /**
     * @brief Severity level for a deferred warning.
     *
     * Mirrors ToastSeverity but is kept independent so this header does not
     * require including ui_toast_manager.h in pre-UI code.
     */
    enum class Severity { INFO, SUCCESS, WARNING, ERROR };

    /** @brief Get the process-wide singleton instance. */
    static PendingStartupWarnings& instance();

    PendingStartupWarnings(const PendingStartupWarnings&) = delete;
    PendingStartupWarnings& operator=(const PendingStartupWarnings&) = delete;

    /**
     * @brief Enqueue a warning to be shown once the toast system is up.
     *
     * Safe to call from any thread. @p duration_ms is the toast duration the
     * drain should use; 0 keeps the toast on screen until closed (see
     * ToastManager::show_with_detail).
     */
    void enqueue(Severity severity, std::string message, uint32_t duration_ms = 8000);

    /**
     * @brief Pop all queued warnings and invoke the callback for each, in FIFO order.
     *
     * Must be called on the main/UI thread. The callback typically forwards to
     * ToastManager::show. After this call returns, the queue is empty.
     */
    void drain(const std::function<void(Severity, const std::string&, uint32_t)>& on_warning);

    /** @brief Test helper: clear all queued warnings without invoking any callback. */
    void clear();

  private:
    PendingStartupWarnings() = default;
    ~PendingStartupWarnings() = default;

    struct Entry {
        Severity severity;
        std::string message;
        uint32_t duration_ms;
    };

    std::mutex mu_;
    std::vector<Entry> pending_;
};

} // namespace helix
