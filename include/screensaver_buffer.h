// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <vector>

/**
 * @file screensaver_buffer.h
 * @brief Accounting for the buffers a saver holds only while it runs
 *
 * A saver costs no RAM while it is not running: everything is allocated in on_start() and
 * released, capacity included, in on_stop(). clear() keeps a vector's capacity, so release
 * swaps it with an empty one.
 */

namespace helix::ui {

/// Bytes the vectors hold together, capacity included.
template <typename... T> size_t buffer_bytes(const std::vector<T>&... v) {
    return (size_t{0} + ... + (v.capacity() * sizeof(T)));
}

/// Frees `v`, capacity included.
template <typename T> void release_buffer(std::vector<T>& v) {
    std::vector<T>().swap(v);
}

/// Frees every vector, capacity included.
template <typename... T> void release_buffers(std::vector<T>&... v) {
    (release_buffer(v), ...);
}

} // namespace helix::ui
