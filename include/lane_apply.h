// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ams_types.h"
#include "lane_resolver.h"
#include "lane_source_store.h"

namespace helix::ams {

struct FilamentSlotOverride;

/// Combine the resolver's presence answer with the backend's lane flavour.
///
/// SlotStatus carries four unrelated facts: presence, which lane is at the
/// extruder (LOADED), a fault (BLOCKED), and Happy Hare's gate_status 2
/// (FROM_BUFFER). Only presence is the resolver's, and it is the only one this
/// narrows: an absent lane reads EMPTY whatever the backend wrote, and a
/// present lane never reads EMPTY or UNKNOWN. The other three flavours belong
/// to firmware state machines that read them back, so they pass through.
[[nodiscard]] SlotStatus narrow_status(SlotStatus backend_status, bool present);

/// Lay a resolved lane onto the SlotInfo a backend has just built.
///
/// Only the fields a source actually observed are written; an unobserved field
/// leaves the backend's own value standing, and presence narrows the status
/// only when a sensor has spoken. The fields SlotInfo carries that the resolver
/// does not own (tool mapping, extruder name, endless-spool group, error,
/// environment, remaining length, temps, indices) are left exactly as the
/// backend set them. Pure: no clock, no globals, no I/O.
void apply_resolved(SlotInfo& slot, const ResolvedLane& resolved);

/// Copy the fields apply_resolved() can write, status aside, from @p src.
///
/// The inverse of a paint: it puts a caller's own values back over one. A
/// backend that paints from the lane in the middle of applying a write has
/// painted a lane that does not know about that write yet, so the values it
/// laid down are the ones being replaced. Snapshot before, copy back after,
/// and the paint keeps only what it is there for.
void copy_resolver_owned_identity(SlotInfo& dst, const SlotInfo& src);

/// Blank the identity fields that only the lane's records state on a backend
/// whose SlotInfo persists across frames.
///
/// apply_resolved() leaves an unobserved field standing, which is right only
/// when the struct holds what firmware says now. On a caching backend the
/// struct also holds whatever an earlier paint wrote, so once the record that
/// stated a field is dropped the struct keeps showing it forever. Clearing
/// these before the paint makes the lane's current records the sole supplier.
///
/// @p ovr is the slot's override record: the locally owned store that restates
/// identity firmware has no key for. A field it carries takes its value rather
/// than a blank - a user edit's brand must survive the frames between the edit
/// and the server refiling the spool's record - so the clear retires only
/// fields no source states at all. Colour, material and weights are NOT here:
/// firmware and the vendor caches state those, and a frame that says nothing
/// about them must leave the last value standing.
void clear_lane_only_identity(SlotInfo& slot, const FilamentSlotOverride* ovr);

/// This lane's resolved values. Never calls into a backend: backends call it
/// while holding their own mutex_.
[[nodiscard]] ResolvedLane resolved_lane(LaneId lane);

} // namespace helix::ams
