// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>

namespace helix {

/// The release an update check is offering, as far as choosing a page needs it.
struct OfferedRelease {
    std::string tag_name; ///< Release tag ("v1.2.3"); may be empty
    std::string version;  ///< Version with or without a leading 'v'; used when tag_name is empty
    /// False for a channel published somewhere other than GitHub releases (the dev
    /// channel), whose tags have no release page there.
    bool on_github = true;
};

/// GitHub release page for the offered release, for builds that never run the
/// tarball updater and send the user to download the update themselves.
///
/// Falls back to the latest release when the release is not on GitHub, or has
/// no tag or version a URL can be built from. The version comes from a remote
/// manifest, so only the characters a release tag uses ever reach the URL.
std::string release_page_url(const OfferedRelease& offered);

} // namespace helix
