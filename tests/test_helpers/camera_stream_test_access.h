// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "lvgl.h"

#if HELIX_HAS_CAMERA

#include "camera_stream.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <memory>
#include <string>
#include <thread>

namespace helix {

/// Test-only accessor for CameraStream's worker-thread state.
///
/// The interesting exit path — "the reconnect loop burned its failure budget
/// and there is no snapshot URL to fall back to" — is unreachable from the
/// public API without three real HTTP round trips plus 3s of reconnect
/// backoff. Pre-loading `stream_fail_count_` makes the loop's entry condition
/// false on the first evaluation, so the worker body runs only the post-loop
/// fallback block and exits: the same code path, deterministically and with
/// no network I/O at all.
///
/// Same friend-TestAccess pattern as tests/test_helpers/camera_widget_test_access.h;
/// requires `friend class CameraStreamTestAccess;` on CameraStream.
class CameraStreamTestAccess {
  public:
    /// Consecutive stream failures the worker tolerates before giving up.
    static constexpr int max_stream_failures() {
        return CameraStream::kMaxStreamFailures;
    }

    /// Spawn the worker on a real std::thread (stored in `stream_thread_`,
    /// exactly as start() does) with its failure budget already exhausted and
    /// no snapshot URL, so it runs only the post-loop fallback block and
    /// returns almost immediately.
    ///
    /// Deliberately does NOT join: production never joins a self-exited
    /// worker either — the thread object stays joinable until someone calls
    /// stop(), and that leftover joinable thread is half of what the caller
    /// is testing. Callers must reap it (stop(), start(), or the destructor).
    static void spawn_worker_to_exhaustion(CameraStream& s) {
        // Never contacted: the reconnect loop's entry condition is already
        // false, so no request is ever built from this URL.
        s.stream_url_ = "http://127.0.0.1:9/never-contacted";
        s.snapshot_url_.clear();
        s.stream_fail_count_ = CameraStream::kMaxStreamFailures;
        s.running_.store(true);
        s.stream_thread_ = std::thread(&CameraStream::stream_thread_func, &s);
    }

    /// Run the worker body *inline on the calling thread* with a snapshot URL
    /// available, so it takes the snapshot-fallback branch and parks in
    /// snapshot_poll_loop() until someone clears the flag via
    /// release_worker(). Returns once the loop has unwound.
    ///
    /// Inline rather than on a std::thread on purpose: snapshot_poll_loop()
    /// evaluates `poll_token.expired()` in its loop condition, and
    /// HelixTestFixture turns on strict L081 detection, which aborts the
    /// process on a bg-thread expired() check. The property under test (the
    /// flag stays set for as long as the snapshot loop is legitimately
    /// running) does not depend on which thread the body runs on.
    static void run_worker_inline_with_snapshot(CameraStream& s, const std::string& snapshot_url) {
        s.stream_url_ = "http://127.0.0.1:9/never-contacted";
        s.snapshot_url_ = snapshot_url;
        s.stream_fail_count_ = CameraStream::kMaxStreamFailures;
        s.running_.store(true);
        s.stream_thread_func();
    }

    /// Consecutive failed snapshots tolerated before the stream reports an error.
    static constexpr int max_snapshot_failures() {
        return CameraStream::kMaxSnapshotFailures;
    }

    /// Run one snapshot fetch of @p snapshot_url on the calling thread, as the
    /// poll loop does, with the given error and frame callbacks.
    static void fetch_snapshot_once(CameraStream& s, const std::string& snapshot_url,
                                    CameraStream::ErrorCallback on_error,
                                    CameraStream::FrameCallback on_frame = nullptr) {
        s.snapshot_url_ = snapshot_url;
        s.on_error_ = std::move(on_error);
        s.on_frame_ = std::move(on_frame);
        s.running_.store(true);
        s.fetch_snapshot();
        s.running_.store(false);
    }

    /// Ask a worker parked in run_worker_inline_with_snapshot() to unwind.
    static void release_worker(CameraStream& s) {
        s.running_.store(false);
    }

    /// Install the scaling-factor table that a real libturbojpeg reports, so
    /// compute_scaled_size()'s decode-time downscale path can be exercised on a
    /// host that has no libturbojpeg to dlopen (and, more to the point, so the
    /// path stays covered on the platforms that only just gained one —
    /// prestonbrown/helixscreen#1245).
    ///
    /// The table is tjGetScalingFactors()'s own list, largest-first, verbatim.
    static void install_turbojpeg_scaling_factors(CameraStream& s) {
        s.fn_get_scaling_factors_ = &fake_get_scaling_factors;
    }

    /// Drop the scaling-factor symbol, reproducing a build with no libturbojpeg
    /// available. Needed to make the "no downscale" assertions deterministic:
    /// the constructor really does dlopen, so a host that happens to have the
    /// library installed would otherwise take the opposite branch.
    static void clear_scaling_factors(CameraStream& s) {
        s.fn_get_scaling_factors_ = nullptr;
    }

    /// True while the std::thread object still owns an unjoined worker —
    /// including one that has already returned. Assigning a new std::thread
    /// over a joinable one is std::terminate, so start() must reap it.
    static bool worker_joinable(const CameraStream& s) {
        return s.stream_thread_.joinable();
    }

    /// Shorten how long stop() waits for the worker before detaching it.
    static void set_join_timeout(CameraStream& s, std::chrono::milliseconds timeout) {
        s.join_timeout_ = timeout;
    }

    /// Spawn a worker that ignores stop() and parks until @p release is set,
    /// standing in for a stream thread wedged in a request that will not cancel.
    /// It touches nothing of @p s, so it may outlive the object; it sets
    /// @p exited as it returns.
    static void spawn_wedged_worker(CameraStream& s, std::shared_ptr<std::atomic<bool>> release,
                                    std::shared_ptr<std::atomic<bool>> exited) {
        s.running_.store(true);
        s.stream_thread_ = std::thread([release, exited] {
            while (!release->load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            exited->store(true);
        });
    }

    /// Byte range of `stream_thread_` within a CameraStream, for checking that
    /// nothing writes to the object's storage after it is gone.
    static size_t worker_offset(const CameraStream& s) {
        return static_cast<size_t>(reinterpret_cast<const unsigned char*>(&s.stream_thread_) -
                                   reinterpret_cast<const unsigned char*>(&s));
    }

  private:
    static CameraStream::TjScalingFactor* fake_get_scaling_factors(int* num_factors) {
        static CameraStream::TjScalingFactor factors[] = {
            {2, 1}, {15, 8}, {7, 4}, {13, 8}, {3, 2}, {11, 8}, {5, 4}, {9, 8},
            {1, 1}, {7, 8},  {3, 4}, {5, 8},  {1, 2}, {3, 8},  {1, 4}, {1, 8}};
        *num_factors = static_cast<int>(sizeof(factors) / sizeof(factors[0]));
        return factors;
    }
};

} // namespace helix

#endif // HELIX_HAS_CAMERA
