// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

/**
 * @brief Register the <leader_line> XML widget: a bare lv_line.
 *
 * XML declares the line, its style and its visibility bindings; the owner sets
 * its points from measured layout. lv_line keeps the points pointer, so the
 * owner must keep that array alive while the line exists.
 */
namespace helix::ui {

void register_leader_line_widget();

} // namespace helix::ui
