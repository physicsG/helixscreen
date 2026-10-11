// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file diag_upload_gate.h
 * @brief Whether this build may ship diagnostics to crash.helixscreen.org
 *
 * Three pipes ask this question, each at exactly one chokepoint: the debug
 * bundle upload (DebugBundleCollector::upload_async), the crash-reporter
 * auto-send (CrashReporter::try_auto_send), and the `ctl log` RPC. A
 * self-compiled or fork build defaults to NO: the bundle CDN receives
 * reports that resolve against none of our symbols (#1410), and nothing
 * forbids a fork from building the tree — the build simply declines to use
 * our infrastructure unless it is marked or explicitly opted in.
 *
 * @pattern Compile default, runtime override (same shape as the
 *          HELIX_HOT_RELOAD switch in runtime_config.cpp)
 * @threading Safe from any thread; reads the environment per call
 */

#pragma once

namespace helix::diag {

/// May this binary ship diagnostics off-device right now?
///
/// The compile default comes from ENABLE_DIAGNOSTIC_UPLOADS (yes only for
/// official packaging builds, which set HELIX_PACKAGING=1);
/// HELIX_DIAGNOSTIC_UPLOADS=1/0 in the environment overrides it either way —
/// 1 is the documented dev opt-in (the deploy targets stamp it on our rigs),
/// 0 is the device-side opt-back-out.
bool uploads_enabled();

/// Compile-time marker carried in every uploaded payload (`diag_upload_marked`).
///
/// True only in builds made with ENABLE_DIAGNOSTIC_UPLOADS=yes. Env opt-ins on
/// an unmarked build upload UNMARKED on purpose: the endpoint must be able to
/// tell an official binary from an opted-in dev run, and a client that says
/// otherwise would defeat the marker (prestonbrown/helixscreen#1410).
constexpr bool marked_build() {
#ifdef HELIX_ENABLE_DIAGNOSTIC_UPLOADS
    return true;
#else
    return false;
#endif
}

} // namespace helix::diag
