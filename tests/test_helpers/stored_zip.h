// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/**
 * @file stored_zip.h
 * @brief Builds a ZIP archive of stored (uncompressed) entries in memory, for
 *        tests that need an archive shaped a particular way.
 */

#include "crc32.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace helix::test {

/// An archive holding @p entries (name, bytes), stored, in the order given.
inline std::string stored_zip(const std::vector<std::pair<std::string, std::string>>& entries) {
    auto u16 = [](std::string& out, uint32_t v) {
        out += static_cast<char>(v & 0xFF);
        out += static_cast<char>((v >> 8) & 0xFF);
    };
    auto u32 = [&u16](std::string& out, uint32_t v) {
        u16(out, v & 0xFFFF);
        u16(out, v >> 16);
    };
    constexpr uint32_t kDosDate = (1 << 5) | 1; // 1980-01-01

    std::string zip;
    std::string central;
    for (const auto& [name, data] : entries) {
        const uint32_t crc =
            crc32_update(0, reinterpret_cast<const uint8_t*>(data.data()), data.size());
        const auto offset = static_cast<uint32_t>(zip.size());
        const auto size = static_cast<uint32_t>(data.size());
        const auto name_len = static_cast<uint32_t>(name.size());

        u32(zip, 0x04034b50);
        u16(zip, 20);       // version needed
        u16(zip, 0);        // flags
        u16(zip, 0);        // stored
        u16(zip, 0);        // time
        u16(zip, kDosDate); // date
        u32(zip, crc);
        u32(zip, size);
        u32(zip, size);
        u16(zip, name_len);
        u16(zip, 0); // extra
        zip += name;
        zip += data;

        u32(central, 0x02014b50);
        u16(central, 20); // version made by
        u16(central, 20); // version needed
        u16(central, 0);  // flags
        u16(central, 0);  // stored
        u16(central, 0);  // time
        u16(central, kDosDate);
        u32(central, crc);
        u32(central, size);
        u32(central, size);
        u16(central, name_len);
        u16(central, 0); // extra
        u16(central, 0); // comment
        u16(central, 0); // disk
        u16(central, 0); // internal attributes
        u32(central, 0); // external attributes
        u32(central, offset);
        central += name;
    }
    const auto cd_offset = static_cast<uint32_t>(zip.size());
    zip += central;
    u32(zip, 0x06054b50);
    u16(zip, 0); // this disk
    u16(zip, 0); // central directory disk
    u16(zip, static_cast<uint32_t>(entries.size()));
    u16(zip, static_cast<uint32_t>(entries.size()));
    u32(zip, static_cast<uint32_t>(central.size()));
    u32(zip, cd_offset);
    u16(zip, 0); // comment
    return zip;
}

} // namespace helix::test
