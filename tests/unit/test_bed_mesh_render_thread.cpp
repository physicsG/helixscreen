// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_bed_mesh_render_thread.cpp
 * @brief Unit tests for BedMeshRenderThread
 *
 * Tests thread lifecycle safety and API contracts.
 * No real renderer is used -- these verify start/stop, double-buffering
 * state, and request coalescing without actual rendering.
 */

#include "bed_mesh_render_thread.h"
#include "bed_mesh_renderer.h"

#include <atomic>
#include <chrono>
#include <thread>

#include "../catch_amalgamated.hpp"

using helix::mesh::BedMeshRenderThread;

// ============================================================================
// Lifecycle tests
// ============================================================================

TEST_CASE("BedMeshRenderThread stop without start is safe", "[bed_mesh][slow]") {
    BedMeshRenderThread thread;
    REQUIRE_FALSE(thread.is_running());
    thread.stop(); // should be a no-op
    REQUIRE_FALSE(thread.is_running());
}

TEST_CASE("BedMeshRenderThread start and stop", "[bed_mesh][slow]") {
    BedMeshRenderThread thread;
    thread.start(100, 100);
    REQUIRE(thread.is_running());
    thread.stop();
    REQUIRE_FALSE(thread.is_running());
}

TEST_CASE("BedMeshRenderThread double stop is safe", "[bed_mesh][slow]") {
    BedMeshRenderThread thread;
    thread.start(100, 100);
    REQUIRE(thread.is_running());
    thread.stop();
    REQUIRE_FALSE(thread.is_running());
    thread.stop(); // second stop -- must not crash or hang
    REQUIRE_FALSE(thread.is_running());
}

TEST_CASE("BedMeshRenderThread destructor stops cleanly", "[bed_mesh][slow]") {
    auto thread = std::make_unique<BedMeshRenderThread>();
    thread->start(100, 100);
    REQUIRE(thread->is_running());
    thread.reset(); // destructor should join without hanging
}

// ============================================================================
// Buffer access tests
// ============================================================================

TEST_CASE("BedMeshRenderThread buffer state before any render", "[bed_mesh][slow]") {
    BedMeshRenderThread thread;
    thread.start(64, 64);

    SECTION("has_ready_buffer is false initially") {
        REQUIRE_FALSE(thread.has_ready_buffer());
    }

    SECTION("acquire_frame returns nullptr when no frame rendered") {
        REQUIRE(thread.acquire_frame() == nullptr);
    }

    SECTION("last_render_time_ms is zero initially") {
        REQUIRE(thread.last_render_time_ms() == Catch::Approx(0.0f));
    }

    thread.stop();
}

// ============================================================================
// Request coalescing / no-crash tests
// ============================================================================

TEST_CASE("BedMeshRenderThread request without renderer does not crash", "[bed_mesh][slow]") {
    BedMeshRenderThread thread;
    thread.start(64, 64);

    // No renderer set -- request_render should not crash (render loop
    // will attempt render, fail, and go back to waiting).
    thread.request_render();

    // Give the thread a moment to process
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    REQUIRE(thread.is_running());
    thread.stop();
}

TEST_CASE("BedMeshRenderThread multiple rapid requests do not deadlock", "[bed_mesh][slow]") {
    BedMeshRenderThread thread;
    thread.start(64, 64);

    // Fire many requests rapidly -- they should coalesce
    for (int i = 0; i < 100; i++) {
        thread.request_render();
    }

    // The thread should still be alive and responsive
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    REQUIRE(thread.is_running());

    thread.stop();
}

TEST_CASE("BedMeshRenderThread frame ready callback is invocable", "[bed_mesh][slow]") {
    BedMeshRenderThread thread;

    std::atomic<int> callback_count{0};
    thread.set_frame_ready_callback([&callback_count]() { callback_count++; });

    thread.start(64, 64);
    REQUIRE(thread.is_running());

    thread.stop();

    // No renderer was ever attached, so every render attempt fails and the
    // frame-ready callback must not fire once. A thread that signalled "frame
    // ready" here would hand the UI a buffer nothing ever rendered into.
    REQUIRE(callback_count.load() == 0);

    // stop() joins the worker; it is not just a flag flip.
    REQUIRE_FALSE(thread.is_running());
}

TEST_CASE("BedMeshRenderThread set_colors is safe while running", "[bed_mesh][slow]") {
    BedMeshRenderThread thread;
    thread.start(64, 64);

    bed_mesh_render_colors_t colors{};
    colors.bg_r = 30;
    colors.bg_g = 30;
    colors.bg_b = 30;
    colors.grid_r = 60;
    colors.grid_g = 60;
    colors.grid_b = 60;

    // Should be safe to call from main thread while render thread is alive
    thread.set_colors(colors);

    thread.stop();
}

// ============================================================================
// Buffer ownership
// ============================================================================

namespace {
struct MeshFixture {
    bed_mesh_renderer_t* renderer = bed_mesh_renderer_create();
    float rows[3][3] = {{0.f, 0.1f, 0.f}, {0.1f, 0.2f, 0.1f}, {0.f, 0.1f, 0.f}};
    MeshFixture() {
        const float* p[3] = {rows[0], rows[1], rows[2]};
        bed_mesh_renderer_set_mesh_data(renderer, p, 3, 3);
    }
    ~MeshFixture() {
        bed_mesh_renderer_destroy(renderer);
    }
};

bool wait_for(BedMeshRenderThread& t, std::atomic<int>& frames, int n) {
    for (int i = 0; i < 400 && frames.load() < n; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return frames.load() >= n;
}
} // namespace

TEST_CASE("BedMeshRenderThread keeps exactly two buffers and frees them on stop",
          "[bed_mesh][slow]") {
    BedMeshRenderThread thread;
    REQUIRE(thread.resident_buffer_count() == 0);
    thread.start(64, 48);
    REQUIRE(thread.resident_buffer_count() == 2);
    thread.stop();
    REQUIRE(thread.resident_buffer_count() == 0);
    REQUIRE(thread.acquire_frame() == nullptr);
}

TEST_CASE("BedMeshRenderThread acquire_frame hands over each frame once without copying",
          "[bed_mesh][slow]") {
    MeshFixture mesh;
    BedMeshRenderThread thread;
    std::atomic<int> frames{0};
    thread.set_renderer(mesh.renderer);
    thread.set_frame_ready_callback([&frames]() { frames++; });
    thread.start(64, 48);

    thread.request_render();
    REQUIRE(wait_for(thread, frames, 1));

    const auto* first = thread.acquire_frame();
    REQUIRE(first != nullptr);
    REQUIRE(first->width() == 64);
    REQUIRE(thread.resident_buffer_count() == 2);

    SECTION("no new frame: same buffer, nothing moves") {
        for (int i = 0; i < 5; i++) {
            REQUIRE(thread.acquire_frame() == first);
        }
    }

    SECTION("a new frame is picked up once, in the other buffer") {
        thread.request_render();
        REQUIRE(wait_for(thread, frames, 2));
        const auto* second = thread.acquire_frame();
        REQUIRE(second != nullptr);
        REQUIRE(second != first);
        REQUIRE(thread.acquire_frame() == second);
        REQUIRE(thread.resident_buffer_count() == 2);
    }

    SECTION("the consumer's buffer is not written while a render is pending") {
        const auto* shown = thread.acquire_frame();
        std::vector<uint8_t> snapshot(shown->data(), shown->data() + shown->stride() * 48);
        // A different mesh makes any stray write into the shown buffer change its bytes.
        {
            std::lock_guard<std::mutex> lock(thread.render_mutex());
            mesh.rows[1][1] = 0.9f;
            mesh.rows[0][0] = -0.4f;
            const float* p[3] = {mesh.rows[0], mesh.rows[1], mesh.rows[2]};
            bed_mesh_renderer_set_mesh_data(mesh.renderer, p, 3, 3);
        }
        // Rendering finishes into the other buffer and then stalls until acquire_frame().
        for (int i = 0; i < 3; i++) {
            thread.request_render();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        REQUIRE(std::equal(snapshot.begin(), snapshot.end(), shown->data()));
        REQUIRE(frames.load() == 2);
    }

    thread.stop();
}
