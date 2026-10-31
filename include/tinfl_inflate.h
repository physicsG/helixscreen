// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// miniz's tinfl in the shape decode_png_thumbnail() and ZipEntryInflater take
// (the Inflate concept, documented on decode_png_thumbnail()): on the firmware
// the ESP32-S3 ROM's copy, allocating from PSRAM; on every other build the copy
// in lib/tinfl, allocating from the heap.

#include <cstddef>
#include <cstdint>
#include <cstdlib>

#if defined(HELIX_PLATFORM_ESP32)
#include "esp_heap_caps.h"
#include "miniz.h" // the ROM's streaming inflater
#else
#include "tinfl/tinfl_copy.h"
#endif

namespace helix {

/// @p Zlib: the stream carries a zlib header and an adler32 trailer, as a PNG's
/// IDAT does. Without it the stream is raw deflate, as a ZIP entry is.
template <bool Zlib> struct TinflInflate {
#if defined(HELIX_PLATFORM_ESP32)
    using Decompressor = tinfl_decompressor;
    static constexpr size_t kWindow = TINFL_LZ_DICT_SIZE;
    static void init(Decompressor* d) {
        tinfl_init(d);
    }
    static int step(Decompressor* d, const uint8_t* in, size_t* in_size, uint8_t* window,
                    uint8_t* next, size_t* out_size, bool more_input) {
        return tinfl_decompress(d, in, in_size, window, next, out_size,
                                (Zlib ? TINFL_FLAG_PARSE_ZLIB_HEADER : 0) |
                                    (more_input ? TINFL_FLAG_HAS_MORE_INPUT : 0));
    }
    static void* alloc(size_t size) {
        return heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
    }
    static void free(void* p) {
        heap_caps_free(p);
    }
    // Not heap_caps_get_largest_free_block(): it walks every PSRAM block
    // with interrupts masked for 20-30ms, far longer than the panel's
    // bounce-buffer refill can wait, so every call glitches the screen.
    static size_t free_bytes() {
        return heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    }
#else
    using Decompressor = tinfl_copy::tinfl_decompressor;
    static constexpr size_t kWindow = TINFL_LZ_DICT_SIZE;
    static void init(Decompressor* d) {
        tinfl_init(d);
    }
    static int step(Decompressor* d, const uint8_t* in, size_t* in_size, uint8_t* window,
                    uint8_t* next, size_t* out_size, bool more_input) {
        return tinfl_copy::tinfl_decompress(
            d, in, in_size, window, next, out_size,
            (Zlib ? tinfl_copy::TINFL_FLAG_PARSE_ZLIB_HEADER : 0) |
                (more_input ? tinfl_copy::TINFL_FLAG_HAS_MORE_INPUT : 0));
    }
    static void* alloc(size_t size) {
        return std::malloc(size);
    }
    static void free(void* p) {
        std::free(p);
    }
    /// A host heap has no total worth gating a decode on.
    static size_t free_bytes() {
        return SIZE_MAX / 2;
    }
#endif
};

/// A ZIP entry's raw deflate stream.
using RawInflate = TinflInflate<false>;

} // namespace helix
