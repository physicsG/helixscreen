// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_filament_path_parallel_states.cpp
 * @brief The aggregate filament state must not invent filament on a PARALLEL tool.
 *
 * compute_slot_render_states() lets the system-wide pair
 * (FilamentPathData::filament_segment / filament_color) override the mounted
 * slot's own state. That is a SERIAL-topology idea: with one shared route to
 * one nozzle, "what is in the system" and "what is in the active slot" are the
 * same sentence.
 *
 * On PARALLEL they are different sentences. Each tool owns its path and the
 * backend answers per slot. The Snapmaker U1 made the consequence visible:
 * AmsBackendSnapmaker::get_filament_segment() floors at PathSegment::SPOOL and
 * never returns NONE, so the mounted tool always tripped the override and drew
 * a full lane in the aggregate colour -- white, because an empty U1 slot
 * reports filament_color_rgba "FFFFFFFF". T0 looked exactly as loaded as the
 * one tool that actually held filament.
 */

#include "../../src/ui/ui_filament_path_internal.h"
#include "ams_types.h"

#include "../catch_amalgamated.hpp"

using helix::ui::fpath::compute_slot_render_states;
using helix::ui::fpath::FilamentPathData;
using helix::ui::fpath::SlotRenderStates;

namespace {

/// Four tools, only slot 3 holding filament -- the live U1 state this fixes.
FilamentPathData u1_four_heads(PathTopology topology) {
    FilamentPathData d{};
    d.topology = static_cast<int>(topology);
    d.slot_count = 4;
    d.active_slot = 0; // T0 is on the carriage

    for (int i = 0; i < 4; i++) {
        d.slot_filament_states[i].segment = PathSegment::NONE;
        d.slot_filament_states[i].color = 0xFFFFFF; // U1 reports white for empty
    }
    d.slot_filament_states[3].segment = PathSegment::SPOOL;
    d.slot_filament_states[3].color = 0x83AFFF; // the PETG actually present

    // What AmsBackendSnapmaker publishes with nothing loaded: never NONE.
    d.filament_segment = static_cast<int>(PathSegment::SPOOL);
    d.filament_color = 0xFFFFFF;
    return d;
}

} // namespace

TEST_CASE("PARALLEL: the aggregate never invents filament on the mounted tool",
          "[ui][filament_path][topology][parallel]") {

    SECTION("an empty mounted tool stays empty") {
        FilamentPathData d = u1_four_heads(PathTopology::PARALLEL);
        SlotRenderStates s = compute_slot_render_states(&d);

        // The regression: T0 was the mounted tool, so the aggregate forced
        // has_filament and painted a white lane down to its nozzle.
        CHECK(s[0].is_mounted);
        CHECK_FALSE(s[0].has_filament);
        CHECK_FALSE(s[1].has_filament);
        CHECK_FALSE(s[2].has_filament);
    }

    SECTION("a tool that genuinely holds filament still shows it") {
        FilamentPathData d = u1_four_heads(PathTopology::PARALLEL);
        SlotRenderStates s = compute_slot_render_states(&d);

        CHECK(s[3].has_filament);
        CHECK_FALSE(s[3].is_mounted); // present without being the active tool
    }

    SECTION("the aggregate may still REFINE a mounted tool that has filament") {
        // Mid-load the aggregate carries the live segment; on a slot that really
        // holds filament it must keep winning, or the load animation freezes.
        FilamentPathData d = u1_four_heads(PathTopology::PARALLEL);
        d.active_slot = 3;
        d.filament_segment = static_cast<int>(PathSegment::NOZZLE);
        d.filament_color = 0x83AFFF;

        SlotRenderStates s = compute_slot_render_states(&d);

        CHECK(s[3].has_filament);
        CHECK(s[3].segment == PathSegment::NOZZLE); // refined past SPOOL
        CHECK(s[3].at_nozzle);
    }

    SECTION("HUB topology keeps the original override") {
        // Serial systems depend on this: the shared path's state IS the active
        // slot's state, and per-slot data may legitimately be absent.
        FilamentPathData d = u1_four_heads(PathTopology::HUB);
        d.filament_segment = static_cast<int>(PathSegment::NOZZLE);

        SlotRenderStates s = compute_slot_render_states(&d);

        CHECK(s[0].has_filament); // mounted slot adopts the aggregate, as before
        CHECK(s[0].segment == PathSegment::NOZZLE);
    }
}
