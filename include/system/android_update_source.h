// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "system/release_page.h"

#include <cstdint>
#include <optional>
#include <string>

namespace helix::android {

/// Installer package Android reports for an app that Google Play installed.
inline constexpr const char* kPlayStoreInstaller = "com.android.vending";

/// Play listing in the Play app, and the web listing for when no app handles market://.
inline constexpr const char* kPlayStoreMarketUrl = "market://details?id=org.helixscreen.app";
inline constexpr const char* kPlayStoreWebUrl =
    "https://play.google.com/store/apps/details?id=org.helixscreen.app";

/// True when Google Play installed this app, so Play is the only source that
/// can deliver an update to it.
inline bool installed_by_play(const std::string& installer_package) {
    return installer_package == kPlayStoreInstaller;
}

using OfferedRelease = helix::OfferedRelease;

/// Where "Install Update" sends an Android user.
///
/// Android never runs the tarball updater; an update arrives through whatever
/// installed the app. A Play install goes to the Play listing. Anything else
/// (an APK sideloaded through adb, a browser or a file manager, which often
/// reports no installer at all) goes to release_page_url(), where the per-ABI
/// APKs live.
///
/// Pure: no JNI, compiled on every platform.
std::string update_url(const std::string& installer_package, const OfferedRelease& offered);

/// Version string for an Android versionCode: the inverse of
/// scripts/android-version-code.sh, so 100000599 is "1.0.5" and 100100031 is
/// "1.1.0-beta.1". nullopt for a code that script cannot produce.
std::optional<std::string> version_from_version_code(int64_t code);

/// What Google Play said about an update for this install on this device.
struct PlayUpdateQuery {
    enum class Outcome {
        Available,    ///< Play will serve available_version_code to this device now
        NotAvailable, ///< Play has nothing newer for this device
        Failed,       ///< No answer: Play missing, query error, or timeout
    };
    Outcome outcome = Outcome::Failed;
    int64_t available_version_code = 0;
    std::string detail; ///< Why the query failed, for the log
};

/// What an update check announces.
struct UpdateVerdict {
    enum class Kind {
        Manifest, ///< Not a Play install: the manifest comparison decides
        Play,     ///< Announce `version`, which Play will serve
        None,     ///< Announce nothing; `reason` says why
    };
    Kind kind = Kind::None;
    std::string version;
    /// The manifest's release is the same version, so its tag and notes apply.
    bool manifest_describes_it = false;
    std::string reason;
};

/// Decide what an update check announces.
///
/// A Play install can only update through Play, and Play may lag a GitHub
/// release for days (not uploaded yet, in review, staged rollout), so it
/// announces only the version Play reports for this device and nothing when
/// Play cannot be asked. Every other install keeps the manifest comparison.
/// `manifest_version` is "" when the manifest could not be fetched.
///
/// Pure: no JNI, compiled on every platform.
UpdateVerdict decide_update(const std::string& installer_package, const PlayUpdateQuery& play,
                            const std::string& manifest_version);

#ifdef __ANDROID__
/// Package name of whatever installed this app, per PackageManager, or "" when
/// Android reports none or the JNI bridge is unavailable.
std::string installer_package();

/// Ask Google Play whether it has an update for this install. Blocks for up to
/// `timeout_sec`, so call it from a worker thread, never the UI thread.
PlayUpdateQuery query_play_update(int timeout_sec);

/// Start Play's in-app update flow. Returns false when the request could not be
/// handed to Java; once handed over, Java opens the Play listing itself if the
/// flow cannot start.
bool start_play_update();
#endif

} // namespace helix::android
