// SPDX-License-Identifier: GPL-3.0-or-later

#include "system/sha256_util.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace helix {

// Minimal portable SHA-256 implementation (public domain)
// Based on RFC 6234 / FIPS 180-4

namespace {

static const uint32_t K256[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

inline uint32_t rotr(uint32_t x, int n) {
    return (x >> n) | (x << (32 - n));
}

inline uint32_t ch(uint32_t x, uint32_t y, uint32_t z) {
    return (x & y) ^ (~x & z);
}

inline uint32_t maj(uint32_t x, uint32_t y, uint32_t z) {
    return (x & y) ^ (x & z) ^ (y & z);
}

inline uint32_t sigma0(uint32_t x) {
    return rotr(x, 2) ^ rotr(x, 13) ^ rotr(x, 22);
}

inline uint32_t sigma1(uint32_t x) {
    return rotr(x, 6) ^ rotr(x, 11) ^ rotr(x, 25);
}

inline uint32_t gamma0(uint32_t x) {
    return rotr(x, 7) ^ rotr(x, 18) ^ (x >> 3);
}

inline uint32_t gamma1(uint32_t x) {
    return rotr(x, 17) ^ rotr(x, 19) ^ (x >> 10);
}

struct Sha256Ctx {
    uint32_t state[8];
    uint64_t count;
    unsigned char buf[64];
};

static void sha256_init(Sha256Ctx& ctx) {
    ctx.state[0] = 0x6a09e667;
    ctx.state[1] = 0xbb67ae85;
    ctx.state[2] = 0x3c6ef372;
    ctx.state[3] = 0xa54ff53a;
    ctx.state[4] = 0x510e527f;
    ctx.state[5] = 0x9b05688c;
    ctx.state[6] = 0x1f83d9ab;
    ctx.state[7] = 0x5be0cd19;
    ctx.count = 0;
}

static void sha256_transform(uint32_t state[8], const unsigned char block[64]) {
    uint32_t W[64];
    for (int i = 0; i < 16; ++i) {
        W[i] = (static_cast<uint32_t>(block[i * 4]) << 24) |
               (static_cast<uint32_t>(block[i * 4 + 1]) << 16) |
               (static_cast<uint32_t>(block[i * 4 + 2]) << 8) |
               (static_cast<uint32_t>(block[i * 4 + 3]));
    }
    for (int i = 16; i < 64; ++i) {
        W[i] = gamma1(W[i - 2]) + W[i - 7] + gamma0(W[i - 15]) + W[i - 16];
    }

    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    uint32_t e = state[4], f = state[5], g = state[6], h = state[7];

    for (int i = 0; i < 64; ++i) {
        uint32_t t1 = h + sigma1(e) + ch(e, f, g) + K256[i] + W[i];
        uint32_t t2 = sigma0(a) + maj(a, b, c);
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
}

static void sha256_update(Sha256Ctx& ctx, const unsigned char* data, size_t len) {
    size_t buf_len = static_cast<size_t>(ctx.count % 64);
    ctx.count += len;

    if (buf_len > 0) {
        size_t fill = 64 - buf_len;
        if (len < fill) {
            std::memcpy(ctx.buf + buf_len, data, len);
            return;
        }
        std::memcpy(ctx.buf + buf_len, data, fill);
        sha256_transform(ctx.state, ctx.buf);
        data += fill;
        len -= fill;
    }

    while (len >= 64) {
        sha256_transform(ctx.state, data);
        data += 64;
        len -= 64;
    }

    if (len > 0) {
        std::memcpy(ctx.buf, data, len);
    }
}

static void sha256_final(Sha256Ctx& ctx, unsigned char hash[32]) {
    uint64_t total_bits = ctx.count * 8;
    size_t buf_len = static_cast<size_t>(ctx.count % 64);

    ctx.buf[buf_len++] = 0x80;
    if (buf_len > 56) {
        std::memset(ctx.buf + buf_len, 0, 64 - buf_len);
        sha256_transform(ctx.state, ctx.buf);
        buf_len = 0;
    }
    std::memset(ctx.buf + buf_len, 0, 56 - buf_len);

    for (int i = 0; i < 8; ++i) {
        ctx.buf[56 + i] = static_cast<unsigned char>(total_bits >> (56 - i * 8));
    }
    sha256_transform(ctx.state, ctx.buf);

    for (int i = 0; i < 8; ++i) {
        hash[i * 4] = static_cast<unsigned char>(ctx.state[i] >> 24);
        hash[i * 4 + 1] = static_cast<unsigned char>(ctx.state[i] >> 16);
        hash[i * 4 + 2] = static_cast<unsigned char>(ctx.state[i] >> 8);
        hash[i * 4 + 3] = static_cast<unsigned char>(ctx.state[i]);
    }
}

std::string to_hex(const unsigned char hash[32]) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string hex(64, '0');
    for (int i = 0; i < 32; ++i) {
        hex[i * 2] = kHex[hash[i] >> 4];
        hex[i * 2 + 1] = kHex[hash[i] & 0xF];
    }
    return hex;
}

} // anonymous namespace

std::string sha256_hex(std::string_view data) {
    Sha256Ctx ctx;
    sha256_init(ctx);
    sha256_update(ctx, reinterpret_cast<const unsigned char*>(data.data()), data.size());
    unsigned char hash[32];
    sha256_final(ctx, hash);
    return to_hex(hash);
}

std::string compute_file_sha256(const std::string& file_path) {
    FILE* f = std::fopen(file_path.c_str(), "rb");
    if (!f)
        return {};

    Sha256Ctx ctx;
    sha256_init(ctx);

    unsigned char buf[8192];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) {
        sha256_update(ctx, buf, n);
    }
    std::fclose(f);

    unsigned char hash[32];
    sha256_final(ctx, hash);
    return to_hex(hash);
}

} // namespace helix
