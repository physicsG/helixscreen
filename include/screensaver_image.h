// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#if HELIX_ENABLE_SCREENSAVER

#include <spdlog/common.h>

#include <cstdint>
#include <lvgl.h>
#include <string>

/**
 * @file screensaver_image.h
 * @brief The images savers decode: sprites, art and the printer picture
 */

namespace helix::ui {

/**
 * @brief `path` decoded once into a draw buffer the caller owns, or nullptr
 *
 * LV_CACHE_DEF_SIZE is 0, so an image handed to LVGL as a path is decoded again on every
 * redraw; a saver decodes once and draws from the buffer. Free it with
 * helix::safe_draw_buf_destroy(). A failure is logged at `failure_level`, naming the image as
 * `what`: a missing bundled asset is worth a warning, a thumbnail that will not decode is not.
 */
lv_draw_buf_t* saver_decode_image(const char* path, const char* what,
                                  spdlog::level::level_enum failure_level = spdlog::level::warn);

/// Reads `path`'s header without decoding its pixels; false when it cannot be read.
bool saver_image_info(const char* path, lv_image_header_t& out);

/// The printer picture a saver shows at `screen_w`: the image the user picked, else the one
/// for the configured printer type, else the generic printer. Never empty.
std::string saver_printer_image_path(int32_t screen_w);

} // namespace helix::ui

#endif // HELIX_ENABLE_SCREENSAVER
