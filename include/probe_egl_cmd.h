// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "gpu_libs.h"

#include <string>

namespace helix {
namespace probe {

/// Exit status of `--probe-egl`, which scripts/helix-launcher.sh reads.
/// EGL came up on real GPU hardware: take the EGL rung.
inline constexpr int kProbeEgl = 0;
/// No EGL here, but the binary carries the display rungs: run DRM dumb buffers,
/// which fall back to fbdev in-process when DRM is unusable.
inline constexpr int kProbeDeclined = 1;
/// A build with no display rungs to choose between: leave the backend to the
/// app's own auto-detection.
inline constexpr int kProbeNoRungs = 2;
/// libdrm does not load here, so neither DRM rung can run: take fbdev.
inline constexpr int kProbeNoDrm = 3;

/// What `--probe-egl` answers before it touches the GPU.
struct ProbeGate {
    /// Nothing the probe needs is missing: open the card nodes and bring EGL up.
    bool probe = false;
    /// The exit status when there is nothing to probe.
    int status = kProbeDeclined;
    /// The line printed with that status.
    std::string reason;
};

/// The verdict table ahead of the GPU probe, first match wins: a binary that
/// does not carry the display rungs (HELIX_DLOPEN_GPU_LIBS) answers
/// kProbeNoRungs; no libdrm, kProbeNoDrm; a binary without the EGL
/// presentation path, or a missing GBM, EGL or GLES2, kProbeDeclined; anything
/// else is probed. @p loads is asked only once the rungs are known to exist.
ProbeGate probe_gate(bool carries_rungs, bool egl_presentation, gpu_libs::LoadCheck loads);

/// Bring EGL up on this board's scanout node, report what answered, and tear it
/// down again. Implements the `--probe-egl` one-shot.
///
/// This is the gate the launcher uses to decide whether helix-screen presents
/// through EGL or steps down to DRM dumb buffers. It is a separate process from
/// the UI on purpose: a GPU bring-up that aborts inside the driver takes down
/// only the probe, and the launcher reads that as a refusal. A missing GPU
/// library is a refusal too, answered before touching the GPU.
///
/// Runs before logging, config, the instance lock and any chdir, so it answers
/// on a board that already has HelixScreen running and needs no config dir.
/// Output is one line on stdout; the exit status is the verdict.
int run_probe_egl();

} // namespace probe
} // namespace helix
