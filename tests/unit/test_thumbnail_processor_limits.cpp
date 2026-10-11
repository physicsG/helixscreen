// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../helix_test_fixture.h"
#include "../test_helpers/thumbnail_processor_test_access.h"
#include "thumbnail_processor.h"

#include <cstdint>
#include <vector>

#include "../catch_amalgamated.hpp"

namespace {

void put_be32(std::vector<uint8_t>& v, uint32_t x) {
    for (int s = 24; s >= 0; s -= 8)
        v.push_back(static_cast<uint8_t>(x >> s));
}

void put_chunk(std::vector<uint8_t>& v, const char* type, const std::vector<uint8_t>& body) {
    put_be32(v, static_cast<uint32_t>(body.size()));
    v.insert(v.end(), type, type + 4);
    v.insert(v.end(), body.begin(), body.end());
    put_be32(v, 0); // stb_image does not verify CRCs
}

/// A structurally complete PNG whose IHDR claims w x h but whose pixel data is
/// junk. A decoder that reaches the IDAT fails with a decode error; only a
/// header check can produce the size error.
std::vector<uint8_t> png_claiming(uint32_t w, uint32_t h) {
    std::vector<uint8_t> v = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    std::vector<uint8_t> ihdr;
    put_be32(ihdr, w);
    put_be32(ihdr, h);
    ihdr.insert(ihdr.end(), {8, 6, 0, 0, 0});
    put_chunk(v, "IHDR", ihdr);
    put_chunk(v, "IDAT", std::vector<uint8_t>(32, 0x55));
    put_chunk(v, "IEND", {});
    return v;
}

} // namespace

TEST_CASE_METHOD(HelixTestFixture, "ThumbnailProcessor rejects oversize dimensions from the header",
                 "[thumbnail][limits]") {
    auto* proc = ThumbnailProcessorTestAccess::make();
    helix::ThumbnailTarget target;

    auto big = proc->process_sync(png_claiming(8000, 8000), "big.png", target);
    CHECK_FALSE(big.success);
    CHECK(big.error.find("too large") != std::string::npos);

    auto wide = proc->process_sync(png_claiming(4097, 16), "wide.png", target);
    CHECK(wide.error.find("too large") != std::string::npos);

    // Within the cap the decoder is reached, and the junk IDAT fails there.
    auto ok_size = proc->process_sync(png_claiming(64, 64), "ok.png", target);
    CHECK(ok_size.error.find("too large") == std::string::npos);

    ThumbnailProcessorTestAccess::destroy(proc);
}
