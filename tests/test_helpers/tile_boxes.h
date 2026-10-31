// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "src/ui/panel_widgets/size_rung.h"

namespace helix::test {

/// A home tile's box and the rung it is just big enough for.
struct Box {
    int w;
    int h;
    SizeRung rung;
};

/// The medium-tier (800x480) home grid's tiles: one cell and two cells across.
inline constexpr int kTileCellW = 114;
inline constexpr int kTileWideW = 233;
inline constexpr int kTileCellH = 113;

/// The rungs of that grid's ladder, from a half cell's Glance to the Full tile.
inline constexpr Box kGlance{55, 54, SizeRung::Glance};
inline constexpr Box kColumn{55, kTileCellH, SizeRung::Column};
inline constexpr Box kStrip{kTileCellW, 54, SizeRung::Strip};
inline constexpr Box kCell{kTileCellW, kTileCellH, SizeRung::Cell};
inline constexpr Box kWide{kTileWideW, kTileCellH, SizeRung::Wide};
inline constexpr Box kFull{kTileWideW, 232, SizeRung::Full};
inline constexpr Box kLarge{350, 340, SizeRung::Large};

/// Every rung from Glance to Full, in ladder order.
inline constexpr Box kMediumLadder[] = {kGlance, kStrip, kColumn, kCell, kWide, kFull};

} // namespace helix::test
