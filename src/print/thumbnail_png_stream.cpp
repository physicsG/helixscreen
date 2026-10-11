// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "thumbnail_png_stream.h"

#include <cstdlib>
#include <cstring>

namespace helix {

namespace {

constexpr uint8_t SIGNATURE[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};

uint32_t be32_at(const uint8_t* p) {
    return (uint32_t{p[0]} << 24) | (uint32_t{p[1]} << 16) | (uint32_t{p[2]} << 8) | p[3];
}

int channels_for(int color_type) {
    switch (color_type) {
    case 0:
        return 1; // grey
    case 2:
        return 3; // RGB
    case 3:
        return 1; // palette index
    case 4:
        return 2; // grey + alpha
    case 6:
        return 4; // RGBA
    default:
        return 0;
    }
}

uint32_t crc32_of(const uint8_t* data, size_t size) {
    static const auto table = [] {
        struct Table {
            uint32_t v[256];
        } t{};
        for (uint32_t n = 0; n < 256; ++n) {
            uint32_t c = n;
            for (int k = 0; k < 8; ++k) {
                c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            }
            t.v[n] = c;
        }
        return t;
    }();
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < size; ++i) {
        c = table.v[(c ^ data[i]) & 0xFF] ^ (c >> 8);
    }
    return c ^ 0xFFFFFFFFu;
}

uint8_t paeth(uint8_t a, uint8_t b, uint8_t c) {
    const int p = a + b - c;
    const int pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
    if (pa <= pb && pa <= pc) {
        return a;
    }
    return pb <= pc ? b : c;
}

} // namespace

bool read_png_header(const uint8_t* data, size_t size, PngHeader& out) {
    // Signature, then the IHDR chunk: length, "IHDR", 13 bytes of fields.
    if (!data || size < 8 + 8 + 13 || std::memcmp(data, SIGNATURE, 8) != 0 ||
        std::memcmp(data + 12, "IHDR", 4) != 0) {
        return false;
    }
    const uint8_t* f = data + 16;
    out.width = static_cast<int>(be32_at(f));
    out.height = static_cast<int>(be32_at(f + 4));
    out.bit_depth = f[8];
    out.color_type = f[9];
    out.interlace = f[12];
    return true;
}

bool png_thumbnail_supported(const PngHeader& h) {
    return h.width > 0 && h.height > 0 && h.width <= THUMBNAIL_MAX_SIDE &&
           h.height <= THUMBNAIL_MAX_SIDE && h.bit_depth == 8 && h.interlace == 0 &&
           channels_for(h.color_type) > 0;
}

bool for_each_png_idat(const uint8_t* data, size_t size, PngPalette& palette, PngIdatSink on_idat,
                       void* user) {
    PngHeader header;
    if (!read_png_header(data, size, header)) {
        return false;
    }
    size_t at = 8;
    while (size - at >= 12) {
        const uint32_t len = be32_at(data + at);
        if (len > size - at - 12) {
            return false; // a chunk running past the end: the fetch was cut short
        }
        const uint8_t* type = data + at + 4;
        const uint8_t* body = data + at + 8;
        if (crc32_of(type, len + 4) != be32_at(body + len)) {
            return false;
        }
        if (std::memcmp(type, "PLTE", 4) == 0) {
            palette.entries = static_cast<int>(len / 3 > 256 ? 256 : len / 3);
            std::memcpy(palette.rgb, body, static_cast<size_t>(palette.entries) * 3);
            std::memset(palette.alpha, 255, sizeof(palette.alpha));
        } else if (std::memcmp(type, "tRNS", 4) == 0) {
            if (header.color_type == 3) {
                std::memcpy(palette.alpha, body, len > 256 ? 256 : len);
            } else if (header.color_type == 0 && len >= 2) {
                palette.has_key = true;
                palette.key[0] = body[1]; // 16-bit sample; 8-bit images use the low byte
            } else if (header.color_type == 2 && len >= 6) {
                palette.has_key = true;
                palette.key[0] = body[1];
                palette.key[1] = body[3];
                palette.key[2] = body[5];
            }
        } else if (std::memcmp(type, "IDAT", 4) == 0) {
            if (!on_idat(body, len, user)) {
                return false;
            }
        } else if (std::memcmp(type, "IEND", 4) == 0) {
            return true;
        }
        at += 12 + len;
    }
    return false; // no IEND
}

PngRowDecoder::PngRowDecoder(const PngHeader& h, const PngPalette& palette, RowSink on_row,
                             void* user)
    : palette_(palette), on_row_(on_row), user_(user), width_(h.width), height_(h.height),
      color_type_(h.color_type), bpp_(channels_for(h.color_type)),
      stride_(static_cast<size_t>(h.width) * static_cast<size_t>(channels_for(h.color_type))),
      cur_(new(std::nothrow) uint8_t[stride_]), prev_(new(std::nothrow) uint8_t[stride_]()),
      rgba_(new(std::nothrow) uint8_t[static_cast<size_t>(h.width) * 4]) {}

bool PngRowDecoder::feed(const uint8_t* data, size_t size) {
    if (!ok()) {
        return false;
    }
    for (size_t i = 0; i < size && !failed_; ++i) {
        if (row_ >= height_) {
            failed_ = true; // more data than the image holds
            break;
        }
        if (filter_ < 0) {
            if (data[i] > 4) {
                failed_ = true;
                break;
            }
            filter_ = data[i];
            pos_ = 0;
            continue;
        }
        cur_[pos_++] = data[i];
        if (pos_ == stride_) {
            finish_row();
        }
    }
    return !failed_;
}

void PngRowDecoder::finish_row() {
    const size_t bpp = static_cast<size_t>(bpp_);
    for (size_t x = 0; x < stride_; ++x) {
        const uint8_t left = x >= bpp ? cur_[x - bpp] : 0;
        const uint8_t up = prev_[x];
        const uint8_t up_left = x >= bpp ? prev_[x - bpp] : 0;
        switch (filter_) {
        case 1:
            cur_[x] = static_cast<uint8_t>(cur_[x] + left);
            break;
        case 2:
            cur_[x] = static_cast<uint8_t>(cur_[x] + up);
            break;
        case 3:
            cur_[x] = static_cast<uint8_t>(cur_[x] + ((left + up) >> 1));
            break;
        case 4:
            cur_[x] = static_cast<uint8_t>(cur_[x] + paeth(left, up, up_left));
            break;
        default:
            break;
        }
    }

    for (int x = 0; x < width_; ++x) {
        const uint8_t* s = &cur_[static_cast<size_t>(x) * bpp];
        uint8_t* d = &rgba_[static_cast<size_t>(x) * 4];
        switch (color_type_) {
        case 0:
            d[0] = d[1] = d[2] = s[0];
            d[3] = palette_.has_key && s[0] == palette_.key[0] ? 0 : 255;
            break;
        case 2:
            std::memcpy(d, s, 3);
            d[3] = palette_.has_key && s[0] == palette_.key[0] && s[1] == palette_.key[1] &&
                           s[2] == palette_.key[2]
                       ? 0
                       : 255;
            break;
        case 3:
            if (s[0] < palette_.entries) {
                std::memcpy(d, &palette_.rgb[s[0] * 3], 3);
                d[3] = palette_.alpha[s[0]];
            } else {
                d[0] = d[1] = d[2] = d[3] = 0;
            }
            break;
        case 4:
            d[0] = d[1] = d[2] = s[0];
            d[3] = s[1];
            break;
        default:
            std::memcpy(d, s, 4);
            break;
        }
    }
    on_row_(rgba_.get(), user_);
    cur_.swap(prev_);
    filter_ = -1;
    ++row_;
}

} // namespace helix
