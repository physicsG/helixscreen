// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <ctime>

namespace helix {

/// Which version of a printer file something was read from. A file re-sliced
/// and uploaded under the same name differs in size or modified time, so
/// anything derived from its content holds only while both still match.
struct FileVersion {
    uint64_t size_bytes = 0;
    time_t modified = 0;

    bool operator==(const FileVersion& other) const {
        return size_bytes == other.size_bytes && modified == other.modified;
    }
    bool operator!=(const FileVersion& other) const {
        return !(*this == other);
    }
};

} // namespace helix
