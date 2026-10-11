// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <string_view>

namespace helix {

/// Compute SHA256 hex digest of a file. Returns empty string on error.
std::string compute_file_sha256(const std::string& file_path);

/// Lowercase hex SHA256 digest of `data`.
std::string sha256_hex(std::string_view data);

} // namespace helix
