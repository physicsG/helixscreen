// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>
#include <vector>

namespace helix::ui {

/**
 * @file console_line.h
 * @brief How one G-code console line is classified and coloured.
 *
 * Pure decisions, no LVGL: the console overlay and the home console tile both
 * render from these, each in its own widgets, so a line never reads as an
 * error in one and a plain response in the other.
 */

/// True if message starts with "!!" or "Error" (case-insensitive).
bool is_console_error_message(const std::string& message);

/// True if message is a periodic temperature report, e.g. "ok T:210.0 /210.0 B:60.0 /60.0".
bool is_console_temp_message(const std::string& message);

/// One run of Mainsail-style span markup. color_class is empty for plain text,
/// else "success", "info", "warning" or "error".
struct ConsoleTextSegment {
    std::string text;
    std::string color_class;
};

/// True if message carries <span class=XXX--text> markup (AFC / Happy Hare output).
bool contains_console_html_spans(const std::string& message);

/// Split <span class=XXX--text>content</span> markup into coloured segments.
std::vector<ConsoleTextSegment> parse_console_html_spans(const std::string& message);

/// Theme colour token for a whole line: errors "danger", responses "success",
/// commands "text".
const char* console_line_color_token(bool is_command, bool is_error);

/// One coloured run of a rendered line, as a theme colour token.
struct ConsoleSpan {
    std::string text;
    const char* color_token;
};

/// The runs one line draws as: a "> " prompt for commands, then the message,
/// split by its span markup when it has any.
std::vector<ConsoleSpan> console_line_spans(const std::string& message, bool is_command,
                                            bool is_error);

} // namespace helix::ui
