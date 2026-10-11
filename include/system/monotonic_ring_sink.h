// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "logging_init.h"

#include <spdlog/details/circular_q.h>
#include <spdlog/details/log_msg_buffer.h>
#include <spdlog/sinks/base_sink.h>

#include <functional>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace helix {
namespace logging {

/**
 * @brief In-memory ring sink that stamps CLOCK_MONOTONIC at log time
 *
 * Replaces spdlog's ringbuffer_sink for the debug-bundle log_tail. The upstream
 * sink stores raw messages and formats them in last_formatted(), which means
 * the `%*` flag reads the clock at DUMP time and every line in a 2000-line dump
 * receives the same monotonic value. Worse, the step detector then compares a
 * real wall-clock delta against a monotonic delta of ~0 and fires on every idle
 * gap over a second: bundle TDQCCQB3 carried 97 CLOCK_STEP annotations summing
 * to 8978 s, which is the whole session.
 *
 * Here sink_it_() records the offset alongside the message and last_formatted()
 * replays it, so the column means what it says. Formatting stays at dump time
 * (the log path keeps its single preallocated block and does no formatting),
 * which is what the constrained MIPS/ARM targets need — the ring runs at debug
 * level while the persistent sinks sit at warning.
 */
class MonotonicRingSink final : public spdlog::sinks::base_sink<std::mutex> {
  public:
    /// Clock source, injectable so tests need no sleeps. Seconds since start.
    using ClockFn = std::function<double()>;

    explicit MonotonicRingSink(size_t n_items, ClockFn clock = &monotonic_seconds)
        : q_{n_items}, clock_{std::move(clock)} {}

    /**
     * @brief Oldest-first formatted lines, each carrying its own log-time offset
     *
     * @param lim Maximum lines to return, newest-biased; 0 means all.
     *
     * Deterministic: repeated calls on unchanged content return identical
     * output, because the replay guard resets the formatter's step memory on
     * entry rather than carrying it across dumps.
     */
    std::vector<std::string> last_formatted(size_t lim = 0) {
        std::lock_guard<std::mutex> lock(base_sink<std::mutex>::mutex_);
        const size_t available = q_.size();
        const size_t count = lim > 0 ? (lim < available ? lim : available) : available;

        ReplayGuard guard;
        std::vector<std::string> out;
        out.reserve(count);
        for (size_t i = available - count; i < available; ++i) {
            const Entry& e = q_.at(i);
            guard.set_value(e.mono);
            spdlog::memory_buf_t formatted;
            base_sink<std::mutex>::formatter_->format(e.msg, formatted);
            out.emplace_back(formatted.data(), formatted.size());
        }
        return out;
    }

    /// Drop every held entry.
    void clear() {
        std::lock_guard<std::mutex> lock(base_sink<std::mutex>::mutex_);
        while (!q_.empty()) {
            q_.pop_front();
        }
    }

    /// Entry count currently held.
    size_t size() {
        std::lock_guard<std::mutex> lock(base_sink<std::mutex>::mutex_);
        return q_.size();
    }

  protected:
    void sink_it_(const spdlog::details::log_msg& msg) override {
        // Stamp here, not in last_formatted(). This is the entire point of the
        // class; see the header comment.
        q_.push_back(Entry{spdlog::details::log_msg_buffer{msg}, clock_ ? clock_() : 0.0});
    }

    void flush_() override {}

  private:
    struct Entry {
        spdlog::details::log_msg_buffer msg;
        double mono = 0.0;
    };

    /// Scoped activation of the thread-local replay state, exception-safe so a
    /// throwing formatter cannot leave live sinks replaying a stale offset.
    class ReplayGuard {
      public:
        ReplayGuard() {
            auto& r = detail::monotonic_replay();
            r.active = true;
            r.reset_sequence = true;
        }
        void set_value(double v) const {
            detail::monotonic_replay().value = v;
        }
        ~ReplayGuard() {
            auto& r = detail::monotonic_replay();
            r.active = false;
            r.reset_sequence = false;
        }
        ReplayGuard(const ReplayGuard&) = delete;
        ReplayGuard& operator=(const ReplayGuard&) = delete;
    };

    spdlog::details::circular_q<Entry> q_;
    ClockFn clock_;
};

using MonotonicRingSinkMt = MonotonicRingSink;

} // namespace logging
} // namespace helix
