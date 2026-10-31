// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "zip_reader.h"

#include <algorithm>

namespace helix::zip {

namespace {

constexpr uint32_t kEocdSig = 0x06054b50;
constexpr uint32_t kZip64LocatorSig = 0x07064b50;
constexpr uint32_t kCentralSig = 0x02014b50;
constexpr uint32_t kLocalSig = 0x04034b50;
constexpr size_t kEocdBytes = 22;
constexpr size_t kZip64LocatorBytes = 20;
constexpr size_t kCentralBytes = 46;
constexpr size_t kMaxComment = 0xFFFF;

uint16_t le16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

uint32_t le32(const uint8_t* p) {
    return uint32_t{p[0]} | (uint32_t{p[1]} << 8) | (uint32_t{p[2]} << 16) | (uint32_t{p[3]} << 24);
}

// The end record, searched from the back: the last signature whose comment
// length reaches exactly to the end of the archive.
std::optional<size_t> find_eocd(const uint8_t* tail, size_t n) {
    if (n < kEocdBytes) {
        return std::nullopt;
    }
    const size_t lowest = n - kEocdBytes > kMaxComment ? n - kEocdBytes - kMaxComment : 0;
    for (size_t at = n - kEocdBytes + 1; at-- > lowest;) {
        if (le32(tail + at) == kEocdSig && at + kEocdBytes + le16(tail + at + 20) == n) {
            return at;
        }
    }
    return std::nullopt;
}

} // namespace

const char* zip_error_name(ZipError error) {
    switch (error) {
    case ZipError::None:
        return "ok";
    case ZipError::NotZip:
        return "not a zip archive";
    case ZipError::Truncated:
        return "truncated";
    case ZipError::Zip64:
        return "zip64 archives are not supported";
    case ZipError::Encrypted:
        return "encrypted entry";
    case ZipError::BadLocalHeader:
        return "bad local header";
    case ZipError::Method:
        return "unsupported compression method";
    case ZipError::Crc:
        return "checksum mismatch";
    case ZipError::Inflate:
        return "corrupt deflate stream";
    case ZipError::Md5:
        return "md5 mismatch";
    case ZipError::Missing:
        return "entry missing";
    case ZipError::Io:
        return "file i/o failed";
    }
    return "unknown";
}

bool parse_central_directory(const uint8_t* tail, size_t n, uint64_t file_size,
                             std::vector<ZipEntry>& entries, ZipError& error, uint64_t* needed) {
    entries.clear();
    if (needed) {
        *needed = 0;
    }
    if (n > file_size) {
        error = ZipError::NotZip;
        return false;
    }
    const std::optional<size_t> eocd = find_eocd(tail, n);
    if (!eocd) {
        error = n < file_size ? ZipError::Truncated : ZipError::NotZip;
        return false;
    }
    const uint8_t* e = tail + *eocd;
    const uint16_t disk = le16(e + 4);
    const uint16_t cd_disk = le16(e + 6);
    const uint16_t count_here = le16(e + 8);
    const uint16_t count = le16(e + 10);
    const uint32_t cd_size = le32(e + 12);
    const uint32_t cd_offset = le32(e + 16);

    const bool zip64_locator =
        *eocd >= kZip64LocatorBytes && le32(e - kZip64LocatorBytes) == kZip64LocatorSig;
    if (zip64_locator || count == 0xFFFF || cd_size == 0xFFFFFFFFu || cd_offset == 0xFFFFFFFFu) {
        error = ZipError::Zip64;
        return false;
    }
    if (disk != 0 || cd_disk != 0 || count_here != count) {
        error = ZipError::NotZip; // a spanned archive
        return false;
    }

    // Where the archive's tail starts, as an offset into the whole file.
    const uint64_t tail_start = file_size - n;
    const uint64_t eocd_offset = tail_start + *eocd;
    if (uint64_t{cd_offset} + cd_size > eocd_offset) {
        error = ZipError::NotZip;
        return false;
    }
    if (cd_offset < tail_start) {
        if (needed) {
            *needed = file_size - cd_offset;
        }
        error = ZipError::Truncated;
        return false;
    }

    const uint8_t* p = tail + (cd_offset - tail_start);
    const uint8_t* const end = p + cd_size;
    entries.reserve(count);
    for (uint16_t i = 0; i < count; ++i) {
        if (static_cast<size_t>(end - p) < kCentralBytes || le32(p) != kCentralSig) {
            error = ZipError::NotZip;
            entries.clear();
            return false;
        }
        const uint16_t flags = le16(p + 8);
        const size_t name_len = le16(p + 28);
        const size_t extra_len = le16(p + 30);
        const size_t comment_len = le16(p + 32);
        const size_t record = kCentralBytes + name_len + extra_len + comment_len;
        if (static_cast<size_t>(end - p) < record) {
            error = ZipError::NotZip;
            entries.clear();
            return false;
        }
        ZipEntry entry;
        entry.method = le16(p + 10);
        entry.crc32 = le32(p + 16);
        entry.csize = le32(p + 20);
        entry.usize = le32(p + 24);
        entry.lh_offset = le32(p + 42);
        if (entry.csize == 0xFFFFFFFFu || entry.usize == 0xFFFFFFFFu ||
            entry.lh_offset == 0xFFFFFFFFu) {
            error = ZipError::Zip64;
            entries.clear();
            return false;
        }
        if (flags & 0x1) {
            error = ZipError::Encrypted;
            entries.clear();
            return false;
        }
        // Its local header and data must end before the central directory
        // starts; a record claiming more is a lie that would size reads.
        if (entry.lh_offset + kZipLocalHeaderBytes + name_len + entry.csize > cd_offset) {
            error = ZipError::NotZip;
            entries.clear();
            return false;
        }
        entry.name.assign(reinterpret_cast<const char*>(p + kCentralBytes), name_len);
        entries.push_back(std::move(entry));
        p += record;
    }
    error = ZipError::None;
    return true;
}

std::optional<uint64_t> archive_size_from_tail(const uint8_t* tail, size_t n) {
    const std::optional<size_t> eocd = find_eocd(tail, n);
    if (!eocd) {
        return std::nullopt;
    }
    const uint32_t cd_size = le32(tail + *eocd + 12);
    const uint32_t cd_offset = le32(tail + *eocd + 16);
    if (cd_size == 0xFFFFFFFFu || cd_offset == 0xFFFFFFFFu) {
        return std::nullopt;
    }
    // The end record follows the central directory directly.
    const uint64_t size = uint64_t{cd_offset} + cd_size + (n - *eocd);
    if (size < n) {
        return std::nullopt;
    }
    return size;
}

bool entry_fits(const ZipEntry& entry, size_t max_bytes) {
    return entry.csize <= max_bytes && entry.usize <= max_bytes;
}

uint64_t entry_read_span(const ZipEntry& entry) {
    return kZipLocalHeaderBytes + entry.name.size() + kZipLocalExtraSlack + entry.csize;
}

std::optional<uint64_t> entry_data_offset(const uint8_t* lh, size_t n, const ZipEntry& entry) {
    if (n < kZipLocalHeaderBytes || le32(lh) != kLocalSig) {
        return std::nullopt;
    }
    const size_t name_len = le16(lh + 26);
    const size_t extra_len = le16(lh + 28);
    if (n < kZipLocalHeaderBytes + name_len || name_len != entry.name.size() ||
        !std::equal(entry.name.begin(), entry.name.end(),
                    reinterpret_cast<const char*>(lh + kZipLocalHeaderBytes))) {
        return std::nullopt;
    }
    return entry.lh_offset + kZipLocalHeaderBytes + name_len + extra_len;
}

std::string_view entry_bytes(const uint8_t* lh, size_t n, const ZipEntry& entry, ZipError& error) {
    const std::optional<uint64_t> data = entry_data_offset(lh, n, entry);
    if (!data) {
        error = n < kZipLocalHeaderBytes ? ZipError::Truncated : ZipError::BadLocalHeader;
        return {};
    }
    const uint64_t start = *data - entry.lh_offset;
    if (start > n || entry.csize > n - start) {
        error = ZipError::Truncated;
        return {};
    }
    error = ZipError::None;
    return {reinterpret_cast<const char*>(lh + start), static_cast<size_t>(entry.csize)};
}

} // namespace helix::zip
