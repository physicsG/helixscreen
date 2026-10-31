// Copyright 2025 HelixScreen
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file ansi_colors.h
 * @brief ANSI escape codes for terminal colors and formatting
 *
 * Portable ANSI color codes that work across macOS/Linux terminals.
 * Based on btop++ approach - using raw ANSI escape sequences.
 */

#pragma once

#include <string>
#include <unistd.h>

namespace ansi {

// Check if output is a TTY (for auto-disable when piped)
inline bool is_tty() {
    return isatty(STDOUT_FILENO);
}

// Colors
constexpr const char* RESET = "\033[0m";
constexpr const char* kBold = "\033[1m";
constexpr const char* kDim = "\033[2m";
constexpr const char* kItalic = "\033[3m";
constexpr const char* kUnderline = "\033[4m";

// Foreground colors
constexpr const char* kBlack = "\033[30m";
constexpr const char* RED = "\033[31m";
constexpr const char* GREEN = "\033[32m";
constexpr const char* kYellow = "\033[33m";
constexpr const char* BLUE = "\033[34m";
constexpr const char* kMagenta = "\033[35m";
constexpr const char* kCyan = "\033[36m";
constexpr const char* WHITE = "\033[37m";

// Bright foreground colors
constexpr const char* kBrightBlack = "\033[90m";
constexpr const char* kBrightRed = "\033[91m";
constexpr const char* kBrightGreen = "\033[92m";
constexpr const char* kBrightYellow = "\033[93m";
constexpr const char* kBrightBlue = "\033[94m";
constexpr const char* kBrightMagenta = "\033[95m";
constexpr const char* kBrightCyan = "\033[96m";
constexpr const char* kBrightWhite = "\033[97m";

// Semantic colors (btop-style)
inline std::string success(const std::string& text) {
    return std::string(kBrightGreen) + text + RESET;
}

inline std::string error(const std::string& text) {
    return std::string(kBrightRed) + text + RESET;
}

inline std::string warning(const std::string& text) {
    return std::string(kBrightYellow) + text + RESET;
}

inline std::string info(const std::string& text) {
    return std::string(kBrightCyan) + text + RESET;
}

inline std::string dim(const std::string& text) {
    return std::string(kDim) + text + RESET;
}

inline std::string header(const std::string& text) {
    return std::string(kBold) + std::string(kBrightCyan) + text + RESET;
}

inline std::string section(const std::string& text) {
    return std::string(kBold) + std::string(kCyan) + text + RESET;
}

inline std::string key(const std::string& text) {
    return std::string(kBrightBlue) + text + RESET;
}

inline std::string value(const std::string& text) {
    return std::string(WHITE) + text + RESET;
}

} // namespace ansi
