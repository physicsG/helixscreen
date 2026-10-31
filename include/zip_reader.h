// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Reading a ZIP archive a piece at a time, for archives that live on the
// printer: the central directory comes from one read of the archive's tail, an
// entry's local header and data from one ranged read each, and an entry inflates
// as its bytes arrive. Nothing here holds a whole entry or a whole archive, and
// nothing throws (the firmware builds without exceptions).

#include "crc32.h"
#include "try_reserve.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace helix::zip {

/// One central directory record.
struct ZipEntry {
    std::string name;
    uint16_t method = 0; ///< 0 stored, 8 deflated
    uint32_t crc32 = 0;
    uint64_t csize = 0;     ///< bytes in the archive
    uint64_t usize = 0;     ///< bytes once inflated
    uint64_t lh_offset = 0; ///< where the entry's local header starts
};

enum class ZipError {
    None,
    NotZip,         ///< no end-of-central-directory record
    Truncated,      ///< the bytes given end before a record does
    Zip64,          ///< a ZIP64 archive or entry, which this reader refuses
    Encrypted,      ///< an encrypted entry
    BadLocalHeader, ///< a local header that does not match its central record
    Method,         ///< a compression method other than stored or deflate
    Crc,            ///< the inflated bytes do not match the recorded CRC or size
    Inflate,        ///< the deflate stream is corrupt, or no memory to inflate it
    Md5,            ///< the bytes do not match the archive's own checksum file
    Missing,        ///< the archive lacks the entry asked for
    Io,             ///< a local file could not be read or written
};

/// Short English name for logs.
const char* zip_error_name(ZipError error);

/// Bytes of the archive's tail that cover the end record of any archive whose
/// comment and central directory fit in it; 3MF archives keep both small.
inline constexpr size_t kZipTailReadBytes = 64 * 1024;

/// Fixed part of a local file header; the name and extra field follow it.
inline constexpr size_t kZipLocalHeaderBytes = 30;

/// Room a ranged read allows for a local header's variable part beyond the
/// entry name, which the central record does not give: the extra field.
inline constexpr size_t kZipLocalExtraSlack = 512;

/**
 * @brief Parses the central directory out of the last @p n bytes of an archive.
 *
 * @p tail is the archive's final @p n bytes, of @p file_size in all. Finds the
 * end record (behind a comment of any length that fits), then reads every
 * central record into @p entries.
 *
 * @param needed Set, on Truncated, to the tail length that would hold the whole
 *        central directory, when the end record itself was found; 0 otherwise.
 * @return false with @p error set: NotZip, Truncated, Zip64 or Encrypted.
 */
bool parse_central_directory(const uint8_t* tail, size_t n, uint64_t file_size,
                             std::vector<ZipEntry>& entries, ZipError& error,
                             uint64_t* needed = nullptr);

/// The size of the archive whose last @p n bytes are @p tail, from where its
/// end record says the central directory sits; for a tail read made without
/// knowing the size. nullopt when the end record is missing or is ZIP64's.
std::optional<uint64_t> archive_size_from_tail(const uint8_t* tail, size_t n);

/// Bytes one ranged read takes to cover @p entry's local header and its data.
uint64_t entry_read_span(const ZipEntry& entry);

/// Whether @p entry is small enough, stored and inflated, to read whole into
/// memory under @p max_bytes; refuse it before asking for any of it.
bool entry_fits(const ZipEntry& entry, size_t max_bytes);

/**
 * @brief Where @p entry's data starts, read from its local header.
 *
 * @p lh is the archive from @p entry.lh_offset on, @p n bytes of it.
 * @return the data's offset in the archive, or nullopt when @p lh is not a
 *         local header for @p entry (wrong signature, another name) or ends
 *         before the header does.
 */
std::optional<uint64_t> entry_data_offset(const uint8_t* lh, size_t n, const ZipEntry& entry);

/**
 * @brief An entry's data, found in a buffer read from @p entry.lh_offset on.
 *
 * Combines entry_data_offset() with the bounds check every caller needs: the
 * data must lie wholly inside the @p n bytes.
 * @return the compressed bytes, or an empty view with @p error set.
 */
std::string_view entry_bytes(const uint8_t* lh, size_t n, const ZipEntry& entry, ZipError& error);

/**
 * @brief Inflates one entry as its compressed bytes arrive, checking its CRC.
 *
 * @p Inflate is the streaming inflater concept decode_png_thumbnail() takes,
 * set up for a raw deflate stream (RawInflate in tinfl_inflate.h). A stored
 * entry passes straight through. Working memory is the inflater and its 32KB
 * window, whatever the entry's size.
 *
 * feed() hands each run of inflated bytes to @p sink, a callable
 * `bool(const uint8_t*, size_t)` returning false to stop. finish() says
 * whether the whole entry arrived intact.
 */
template <class Inflate> class ZipEntryInflater {
  public:
    explicit ZipEntryInflater(const ZipEntry& entry) : entry_(entry) {
        if (entry_.method == 8) {
            d_ = static_cast<typename Inflate::Decompressor*>(
                Inflate::alloc(sizeof(typename Inflate::Decompressor)));
            window_ = static_cast<uint8_t*>(Inflate::alloc(Inflate::kWindow));
            if (d_ && window_) {
                Inflate::init(d_);
            } else {
                error_ = ZipError::Inflate;
            }
        } else if (entry_.method != 0) {
            error_ = ZipError::Method;
        }
    }
    ~ZipEntryInflater() {
        Inflate::free(d_);
        Inflate::free(window_);
    }
    ZipEntryInflater(const ZipEntryInflater&) = delete;
    ZipEntryInflater& operator=(const ZipEntryInflater&) = delete;

    /// False once the entry is corrupt, the sink stopped, or the bytes overrun
    /// the entry's recorded size; error() then says which.
    template <class Sink> bool feed(const uint8_t* data, size_t size, Sink& sink) {
        if (error_ != ZipError::None) {
            return false;
        }
        if (size > entry_.csize - consumed_) {
            error_ = ZipError::Crc; // more bytes than the entry holds
            return false;
        }
        consumed_ += size;
        if (entry_.method == 0) {
            return emit(data, size, sink);
        }
        return inflate(data, size, consumed_ < entry_.csize, sink);
    }

    /// True when every byte arrived, inflated to the recorded size, and
    /// matched the recorded CRC. Otherwise false with @p error set.
    bool finish(ZipError& error) {
        if (error_ == ZipError::None && consumed_ != entry_.csize) {
            error_ = ZipError::Truncated;
        }
        if (error_ == ZipError::None && entry_.method == 8 && status_ != 0) {
            error_ = ZipError::Inflate;
        }
        if (error_ == ZipError::None && (produced_ != entry_.usize || crc_ != entry_.crc32)) {
            error_ = ZipError::Crc;
        }
        error = error_;
        return error_ == ZipError::None;
    }

    ZipError error() const {
        return error_;
    }

  private:
    template <class Sink> bool emit(const uint8_t* data, size_t size, Sink& sink) {
        if (size > entry_.usize - produced_) {
            error_ = ZipError::Crc; // inflates past the recorded size
            return false;
        }
        produced_ += size;
        crc_ = crc32_update(crc_, data, size);
        if (!sink(data, size)) {
            error_ = ZipError::Io;
            return false;
        }
        return true;
    }

    // Inflates @p in through the circular window, handing each finished run
    // to the sink, until the inflater wants more input or the stream ends.
    template <class Sink> bool inflate(const uint8_t* in, size_t left, bool more, Sink& sink) {
        for (;;) {
            size_t in_size = left;
            size_t out_size = Inflate::kWindow - at_;
            status_ = Inflate::step(d_, in, &in_size, window_, window_ + at_, &out_size, more);
            in += in_size;
            left -= in_size;
            if (out_size && !emit(window_ + at_, out_size, sink)) {
                return false;
            }
            at_ = (at_ + out_size) & (Inflate::kWindow - 1);
            if (status_ < 0) {
                error_ = ZipError::Inflate;
                return false;
            }
            if (status_ == 0) {
                return true;
            }
            // Out of input: a stream that ends short of its end block is
            // caught by finish(), which finds the inflater not done.
            if (status_ == 1 && left == 0) {
                return true;
            }
        }
    }

    const ZipEntry entry_;
    typename Inflate::Decompressor* d_ = nullptr;
    uint8_t* window_ = nullptr;
    size_t at_ = 0;
    int status_ = 1;
    uint64_t consumed_ = 0;
    uint64_t produced_ = 0;
    uint32_t crc_ = 0;
    ZipError error_ = ZipError::None;
};

/**
 * @brief Inflates a whole small entry held in memory, such as a metadata file.
 *
 * @p data is the entry's compressed bytes (entry_bytes()). Refuses an entry
 * whose inflated size exceeds @p max_bytes before allocating anything for it.
 */
template <class Inflate>
bool inflate_entry_to_string(const ZipEntry& entry, std::string_view data, size_t max_bytes,
                             std::string& out, ZipError& error) {
    out.clear();
    if (entry.usize > max_bytes) {
        error = ZipError::Truncated;
        return false;
    }
    if (!try_reserve(out, static_cast<size_t>(entry.usize))) {
        error = ZipError::Inflate; // no memory for it
        return false;
    }
    ZipEntryInflater<Inflate> inflater(entry);
    auto sink = [&out](const uint8_t* p, size_t n) {
        out.append(reinterpret_cast<const char*>(p), n);
        return true;
    };
    inflater.feed(reinterpret_cast<const uint8_t*>(data.data()), data.size(), sink);
    if (!inflater.finish(error)) {
        out.clear();
        return false;
    }
    return true;
}

} // namespace helix::zip
