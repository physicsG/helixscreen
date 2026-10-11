// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "config_backup.h"

#include "helix_fs.h"
#include "spdlog/spdlog.h"
#include "text_io.h"

#include <cerrno>
#include <cstring>
#include <optional>
#include <sys/stat.h>

namespace hfs = helix::fs;

namespace helix::config_backup {

bool write_backup_file(const std::string& src_path, const std::string& backup_path) {
    struct stat st {};
    if (stat(src_path.c_str(), &st) != 0) {
        return false; // Source doesn't exist, nothing to back up
    }

    // Ensure parent directory exists (create if needed for $HOME fallback)
    std::string parent{hfs::parent_path(backup_path)};
    if (!parent.empty() && !hfs::exists(parent)) {
        if (!hfs::create_directories(parent)) {
            return false;
        }
    }

    const auto data = helix::text_io::read_file(src_path);
    if (!data || !helix::text_io::write_file_atomic(backup_path, *data)) {
        // Demoted to debug — write_rolling_backup() warns once if BOTH primary
        // and fallback fail. A primary-only failure (typical in dev: /var/lib
        // not writable, $HOME fallback succeeds) is not noteworthy.
        spdlog::debug("[Config] Backup to {} failed: {}", backup_path, std::strerror(errno));
        return false;
    }
    return true;
}

void write_rolling_backup(const std::string& src_path, const std::string& primary,
                          const std::string& fallback) {
    if (write_backup_file(src_path, primary)) {
        spdlog::trace("[Config] Backup written: {}", primary);
        return;
    }
    if (write_backup_file(src_path, fallback)) {
        spdlog::trace("[Config] Backup written (fallback): {}", fallback);
        return;
    }
    // Both failed — non-fatal, but worth knowing about.
    spdlog::warn("[Config] Backup of {} failed at both primary ({}) and fallback ({})", src_path,
                 primary, fallback);
}

std::string find_backup(const std::vector<std::string>& paths) {
    struct stat st {};
    for (const auto& p : paths) {
        if (stat(p.c_str(), &st) == 0) {
            return p;
        }
    }
    return {};
}

bool restore_from_backup(const std::string& target_path, const char* label,
                         const std::vector<std::string>& backup_paths) {
    struct stat st {};
    if (stat(target_path.c_str(), &st) == 0) {
        return false; // Target exists, no restore needed
    }

    std::string backup = find_backup(backup_paths);
    if (backup.empty()) {
        return false;
    }

    spdlog::warn("[Config] {} missing — restoring from backup: {}", label, backup);

    std::string parent_dir{hfs::parent_path(target_path)};
    if (!parent_dir.empty() && !hfs::exists(parent_dir)) {
        if (!hfs::create_directories(parent_dir)) {
            spdlog::error("[Config] Failed to create dir {}: {}", parent_dir, std::strerror(errno));
            return false;
        }
    }

    if (!hfs::copy_file(backup, target_path)) {
        spdlog::error("[Config] Failed to restore {}: {}", label, std::strerror(errno));
        return false;
    }
    spdlog::info("[Config] Restored {} from backup: {}", label, backup);
    return true;
}

void remove_backups(const std::vector<std::string>& backup_paths) {
    for (const auto& path : backup_paths) {
        if (hfs::remove(path)) {
            spdlog::info("[Config] Removed backup: {}", path);
        } else if (errno != ENOENT) {
            spdlog::warn("[Config] Failed to remove backup {}: {}", path, std::strerror(errno));
        }
    }
}

} // namespace helix::config_backup
