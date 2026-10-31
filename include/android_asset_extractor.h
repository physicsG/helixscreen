// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string>

namespace helix {

/// Result of asset extraction attempt
enum class AssetExtractionResult {
    EXTRACTED,       // Fresh extraction performed
    ALREADY_CURRENT, // Version matched, no extraction needed
    FAILED           // Extraction failed
};

/// Extract assets from source_dir to target_dir if version doesn't match.
/// Writes a VERSION file to target_dir on success.
/// The source_dir/target_dir abstraction lets us test without Android APIs.
AssetExtractionResult extract_assets_if_needed(const std::string& source_dir,
                                               const std::string& target_dir,
                                               const std::string& current_version);

/// Where the app keeps each kind of file under its private storage
/// (Context.getFilesDir(), which an app update leaves in place).
struct AndroidStorageLayout {
    /// Extracted package content plus the live config/ (HELIX_DATA_DIR).
    std::string data_dir;
    /// Rolling settings backups: the primary and fallback tiers. Outside
    /// data_dir, so nothing an extraction writes or removes can reach them.
    std::string backup_dir;
    std::string backup_fallback_dir;
};

/// The layout under @p internal_storage. Pure.
AndroidStorageLayout android_storage_layout(const std::string& internal_storage);

/// Android-specific entry points that use SDL paths.
/// Only callable on Android builds.
#ifdef __ANDROID__
void android_extract_assets_if_needed();

/// Points the rolling config backups at the app's private storage. Android has
/// no /var/lib/helixscreen and no $HOME, so the default tiers are unwritable.
/// Must run before Config::init().
void android_configure_backup_dirs();
#endif

} // namespace helix
