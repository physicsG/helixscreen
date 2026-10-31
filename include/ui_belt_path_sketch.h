// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace helix::ui {

/// Register `<belt_path_sketch>`: a top view of the toolhead with the two
/// diagonals the Belt Tension check shakes it along, Path A (+X-Y) and Path B
/// (+X+Y), in the belt_path_a / belt_path_b colors, each labelled with the
/// name helix::ui::belt_path_name() gives it. No motors or
/// belt routing are drawn: those depend on the printer. The panel invalidates
/// the sketch when the names change. Size it from XML; the drawing scales to fit.
void register_belt_path_sketch_widget();

} // namespace helix::ui
