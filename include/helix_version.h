// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

/**
 * @file helix_version.h
 * @brief HelixScreen version information
 *
 * Version follows Semantic Versioning 2.0.0 (https://semver.org/):
 * - MAJOR: Incompatible API/config changes
 * - MINOR: New features, backwards compatible
 * - PATCH: Bug fixes only
 *
 * The version is defined in the VERSION file at the project root
 * and passed via -DHELIX_VERSION during compilation.
 */

// Version is defined by Makefile from VERSION file
// Fallback if not defined (e.g., IDE parsing)
#ifndef HELIX_VERSION
#define HELIX_VERSION "dev"
#endif

#ifndef HELIX_VERSION_MAJOR
#define HELIX_VERSION_MAJOR 0
#endif

#ifndef HELIX_VERSION_MINOR
#define HELIX_VERSION_MINOR 0
#endif

#ifndef HELIX_VERSION_PATCH
#define HELIX_VERSION_PATCH 0
#endif

/// User-Agent for every HTTP request the app makes.
#define HELIX_USER_AGENT "HelixScreen/" HELIX_VERSION

// Build type
#ifndef HELIX_BUILD_TYPE
#define HELIX_BUILD_TYPE "dev"
#endif

/**
 * @brief Get full version string
 * @return Version string like "1.1.0" or "1.1.0-beta+abc1234"
 */
inline const char* helix_version() {
    return HELIX_VERSION;
}

/**
 * @brief Get the short git commit hash, or "unknown" outside a git checkout
 *
 * Out of line, and deliberately not a macro: the hash changes on every commit,
 * so exposing it as a -D on the global compiler flags made every translation
 * unit miss ccache's direct mode on every push. It now reaches exactly one
 * object, src/system/helix_version.cpp. See scripts/gen-git-hash.sh.
 */
const char* helix_git_hash();

/**
 * @brief Get version with git hash
 * @return Version string like "1.1.0 (abc1234)"
 *
 * Out of line for the same reason, and because an inline definition reading the
 * hash would have a different body in TUs that saw the define and TUs that did
 * not, which is an ODR violation the linker resolves arbitrarily.
 */
const char* helix_version_full();

/**
 * @brief Check if version is at least the specified version
 * @param major Required major version
 * @param minor Required minor version (default 0)
 * @param patch Required patch version (default 0)
 * @return true if current version >= required version
 */
inline bool helix_version_at_least(int major, int minor = 0, int patch = 0) {
    if (HELIX_VERSION_MAJOR > major)
        return true;
    if (HELIX_VERSION_MAJOR < major)
        return false;
    if (HELIX_VERSION_MINOR > minor)
        return true;
    if (HELIX_VERSION_MINOR < minor)
        return false;
    return HELIX_VERSION_PATCH >= patch;
}
