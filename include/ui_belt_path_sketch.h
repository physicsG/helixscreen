// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace helix::ui {

/// Register `<belt_path_sketch>`: a top view of the toolhead with the two
/// diagonals the Belt Tension check shakes it along, Path A (+X-Y) and Path B
/// (+X+Y), in the belt_path_a / belt_path_b colors. It names paths, not belts:
/// which physical belt a path loads depends on the printer, so no motors or
/// belt routing are drawn. Size it from XML; the drawing scales to fit.
void register_belt_path_sketch_widget();

} // namespace helix::ui
