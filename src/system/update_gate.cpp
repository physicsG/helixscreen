// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

// The pure update-gate predicates declared in app_globals.h. They live apart from
// app_globals.cpp, which the test link leaves out, so the tests exercise these bodies.

#include "app_globals.h"
#include "env_knobs.h"
#include "system/helix_paths.h"

#include <cctype>
#include <filesystem>
#include <string>

bool compute_updates_externally_managed(const char* disable_auto_updates, bool platform_default) {
    // An explicit flag decides it, in either direction. helix::env_truthy()
    // only answers "is this truthy", which cannot distinguish "0" from unset, so
    // presence is tested separately and a falsy value force-enables self-update
    // where the platform would otherwise default it off.
    //
    // Blank counts as ABSENT, not as falsy. helixscreen.env values routinely carry
    // a stray space (which is why parsing trims), and an all-whitespace value read
    // as an explicit "no" would silently switch self-update back on for a
    // firmware-managed install, the exact failure this predicate exists to stop.
    if (disable_auto_updates) {
        const char* p = disable_auto_updates;
        while (*p && std::isspace(static_cast<unsigned char>(*p))) {
            ++p;
        }
        if (*p != '\0') {
            return helix::env_truthy(disable_auto_updates);
        }
    }
    return platform_default;
}

bool compute_self_update_supported(const std::string& install_root, bool can_escalate) {
    // install.sh applies an update one of two ways, and this predicate must
    // recognise BOTH or it hides an updater that would have worked:
    //
    //   atomic swap    "mv <root> <root>.old; mv <new> <root>": renames mutate
    //                  the PARENT's directory entries, so it needs write
    //                  permission on the parent, not on the root.
    //   in-place       delete the root's contents (bar config/) and move the new
    //                  ones in. Everything happens INSIDE the root, so it needs
    //                  write permission on the root alone. install.sh picks this
    //                  automatically when the parent is not writable
    //                  (scripts/lib/installer/release.sh, "replacing install contents in-place").
    //
    // The standalone-display layout needs the second: with no local Klipper,
    // detect_pi_install_dir() falls through to /opt/helixscreen, whose parent is
    // root-owned while the service user owns the root itself (the unit's
    // ExecStartPre chowns it). A gate that hides the updater there stays wrong,
    // since its fix can only arrive through an update.
    if (install_root.empty()) {
        // Unresolvable layout (bind-mounted binary). Conservative: assume
        // supported. The installer's own fallbacks and the explicit
        // HELIX_DISABLE_AUTO_UPDATES flag remain the deciding factors.
        return true;
    }
    // probe_writable(), not is_writable_dir(): the latter is access(W_OK), which
    // answers from the permission bits alone and so cannot see a read-only mount,
    // a restrictive ACL, an immutable bit, or an LSM denial. This predicate exists
    // to PREDICT what install.sh will manage, and install.sh settles the same
    // question by writing a probe file ("touch ${INSTALL_DIR}/.update_test"), so
    // answering it a different way is how the two drift apart. probe_writable
    // creates a uniquely-named file, writes a byte, and removes it; the result is
    // cached process-wide by self_update_supported(), so this costs one create per
    // directory per boot.
    const std::string parent = std::filesystem::path(install_root).parent_path().string();
    if (!parent.empty() && helix::paths::probe_writable(parent)) {
        return true; // atomic swap
    }
    if (parent.empty()) {
        return true; // no parent to test (e.g. a bare relative name): don't block.
    }
    if (helix::paths::probe_writable(install_root)) {
        return true; // in-place replacement
    }
    // Neither path is open to this user. That is still NOT the same as impossible:
    // install.sh escalates with sudo when it can. Only a box with no write access
    // anywhere in the install tree and no route to root is genuinely stuck.
    return can_escalate;
}

bool compute_update_install_suppressed(bool externally_managed, bool self_update_ok) {
    return externally_managed || !self_update_ok;
}
