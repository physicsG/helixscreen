// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#if HELIX_HAS_BED_MESH_3D

#include "bed_mesh_render_thread.h"

#include "helix_thread.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>

namespace helix {
namespace mesh {

BedMeshRenderThread::BedMeshRenderThread() = default;

BedMeshRenderThread::~BedMeshRenderThread() {
    stop();
}

void BedMeshRenderThread::start(int width, int height) {
    if (running_.load()) {
        spdlog::warn("[BedMeshRenderThread] start() called while already running");
        return;
    }

    {
        std::lock_guard<std::mutex> lock(swap_mutex_);
        back_ = std::make_unique<PixelBuffer>(width, height);
        shown_ = std::make_unique<PixelBuffer>(width, height);
        ready_.reset();
    }
    back_available_.store(true);
    shown_has_frame_ = false;
    buffer_ready_.store(false);
    render_requested_.store(false);
    last_render_time_ms_.store(0.0f);

    running_.store(true);
    thread_ = helix::make_thread(&BedMeshRenderThread::render_loop, this);

    spdlog::info("[BedMeshRenderThread] Started ({}x{}, two buffers)", width, height);
}

void BedMeshRenderThread::stop() {
    if (!running_.load()) {
        return;
    }

    spdlog::info("[BedMeshRenderThread] Stopping...");

    // Set running_ under cv_mutex_ so the render loop cannot miss the wakeup.
    // Without the lock, the store + notify can interleave between the render
    // loop's predicate evaluation and its actual sleep on the condvar, leaving
    // the thread waiting forever and hanging thread_.join().
    {
        std::lock_guard<std::mutex> lock(cv_mutex_);
        running_.store(false);
    }
    cv_.notify_all();

    if (thread_.joinable()) {
        thread_.join();
    }

    std::lock_guard<std::mutex> lock(swap_mutex_);
    back_.reset();
    ready_.reset();
    shown_.reset();
    back_available_.store(false);
    shown_has_frame_ = false;
    buffer_ready_.store(false);

    spdlog::info("[BedMeshRenderThread] Stopped");
}

bool BedMeshRenderThread::is_running() const {
    return running_.load();
}

void BedMeshRenderThread::set_renderer(bed_mesh_renderer_t* renderer) {
    std::lock_guard<std::mutex> lock(renderer_mutex_);
    renderer_ = renderer;
}

void BedMeshRenderThread::set_colors(const bed_mesh_render_colors_t& colors) {
    std::lock_guard<std::mutex> lock(colors_mutex_);
    colors_ = colors;
}

void BedMeshRenderThread::request_render() {
    {
        std::lock_guard<std::mutex> lock(cv_mutex_);
        render_requested_.store(true);
    }
    cv_.notify_one();
}

bool BedMeshRenderThread::has_ready_buffer() const {
    return buffer_ready_.load();
}

const PixelBuffer* BedMeshRenderThread::acquire_frame() {
    bool returned_to_render = false;
    const PixelBuffer* result = nullptr;
    {
        std::lock_guard<std::mutex> lock(swap_mutex_);
        if (ready_) {
            shown_.swap(ready_);
            back_ = std::move(ready_);
            back_available_.store(true);
            shown_has_frame_ = true;
            returned_to_render = true;
        }
        result = shown_has_frame_ ? shown_.get() : nullptr;
    }
    if (returned_to_render) {
        // Taking cv_mutex_ orders the flag store before the render loop's predicate check.
        { std::lock_guard<std::mutex> lock(cv_mutex_); }
        cv_.notify_one();
    }
    return result;
}

int BedMeshRenderThread::resident_buffer_count() const {
    std::lock_guard<std::mutex> lock(swap_mutex_);
    return (back_ ? 1 : 0) + (ready_ ? 1 : 0) + (shown_ ? 1 : 0);
}

void BedMeshRenderThread::set_frame_ready_callback(std::function<void()> cb) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    frame_ready_callback_ = std::move(cb);
}

float BedMeshRenderThread::last_render_time_ms() const {
    return last_render_time_ms_.load();
}

void BedMeshRenderThread::reset_quality() {
    // Protect adaptive quality fields (frame_count_, recent_frame_times_,
    // degraded_mode_) which are also read/written by the render thread
    // under renderer_mutex_.
    std::lock_guard<std::mutex> lock(renderer_mutex_);

    frame_count_ = 0;
    recent_frame_times_.fill(0.0f);

    if (degraded_mode_) {
        degraded_mode_ = false;

        // Restore gradient mode on the renderer
        if (renderer_) {
            bed_mesh_renderer_set_dragging(renderer_, false);
        }
        spdlog::debug("[BedMeshRenderThread] Quality reset (gradient mode restored)");
    }
}

void BedMeshRenderThread::return_target(std::unique_ptr<PixelBuffer> buf) {
    std::lock_guard<std::mutex> lock(swap_mutex_);
    back_ = std::move(buf);
    back_available_.store(true);
}

void BedMeshRenderThread::render_loop() {
    spdlog::debug("[BedMeshRenderThread] Render loop started");

    while (running_.load()) {
        // Wait for a render request or stop signal
        {
            std::unique_lock<std::mutex> lock(cv_mutex_);
            cv_.wait(lock, [this]() {
                return (render_requested_.load() && back_available_.load()) || !running_.load();
            });
        }

        if (!running_.load()) {
            break;
        }

        // Consume the request (coalesces multiple requests into one render)
        render_requested_.store(false);

        std::unique_ptr<PixelBuffer> target;
        {
            std::lock_guard<std::mutex> lock(swap_mutex_);
            target = std::move(back_);
            back_available_.store(false);
        }

        // Snapshot colors under lock
        bed_mesh_render_colors_t colors;
        {
            std::lock_guard<std::mutex> lock(colors_mutex_);
            colors = colors_;
        }

        // Hold renderer_mutex_ during the render call and adaptive quality tracking.
        // The main thread acquires this mutex before modifying renderer state
        // (rotation, dragging), preventing concurrent access.
        bool ok = false;
        float elapsed_ms = 0.0f;
        {
            std::lock_guard<std::mutex> lock(renderer_mutex_);

            if (!renderer_) {
                spdlog::warn("[BedMeshRenderThread] Render requested but no renderer set");
                return_target(std::move(target));
                continue;
            }

            auto t0 = std::chrono::steady_clock::now();
            ok = bed_mesh_renderer_render_to_buffer(renderer_, *target, colors);
            auto t1 = std::chrono::steady_clock::now();

            if (!ok) {
                spdlog::warn("[BedMeshRenderThread] render_to_buffer failed");
                return_target(std::move(target));
                continue;
            }

            elapsed_ms = std::chrono::duration<float, std::milli>(t1 - t0).count();
            last_render_time_ms_.store(elapsed_ms);

            // Track frame times for adaptive quality degradation
            recent_frame_times_[frame_count_ % FRAME_HISTORY_SIZE] = elapsed_ms;
            frame_count_++;

            if (frame_count_ >= 3) {
                int count = std::min(frame_count_, FRAME_HISTORY_SIZE);
                float avg = 0.0f;
                for (int i = 0; i < count; i++) {
                    avg += recent_frame_times_[i];
                }
                avg /= static_cast<float>(count);

                if (!degraded_mode_ && avg > DEGRADE_THRESHOLD_MS) {
                    degraded_mode_ = true;
                    bed_mesh_renderer_set_dragging(renderer_, true);
                    spdlog::info(
                        "[BedMeshRenderThread] Degrading to solid-color mode (avg {:.0f}ms)", avg);
                } else if (degraded_mode_ && avg < RESTORE_THRESHOLD_MS) {
                    degraded_mode_ = false;
                    bed_mesh_renderer_set_dragging(renderer_, false);
                    spdlog::info("[BedMeshRenderThread] Restored gradient mode (avg {:.0f}ms)",
                                 avg);
                }
            }
        } // renderer_mutex_ released

        // Publish; the render thread has no buffer again until the consumer returns one
        {
            std::lock_guard<std::mutex> lock(swap_mutex_);
            ready_ = std::move(target);
        }
        buffer_ready_.store(true);

        spdlog::debug("[BedMeshRenderThread] Frame rendered in {:.1f} ms", elapsed_ms);

        // Notify callback (typically queues a widget invalidation)
        std::function<void()> cb;
        {
            std::lock_guard<std::mutex> lock(callback_mutex_);
            cb = frame_ready_callback_;
        }
        if (cb) {
            cb();
        }
    }

    spdlog::debug("[BedMeshRenderThread] Render loop exiting");
}

} // namespace mesh
} // namespace helix

#endif // HELIX_HAS_BED_MESH_3D
