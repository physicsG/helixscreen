// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "system/crash_record.h"

#include <cstdio>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace helix::crash_record {

bool is_crash_signal(int sig) {
    for (const int s : kCrashSignals) {
        if (s == sig) {
            return true;
        }
    }
    return false;
}

const char* signal_name(int sig) {
    switch (sig) {
    case SIGSEGV:
        return "SIGSEGV";
    case SIGABRT:
        return "SIGABRT";
    case SIGBUS:
        return "SIGBUS";
    case SIGFPE:
        return "SIGFPE";
    case SIGILL:
        return "SIGILL";
    case SIGTRAP:
        return "SIGTRAP";
    case SIGSYS:
        return "SIGSYS";
    default:
        return "UNKNOWN";
    }
}

bool write_external(const std::string& path, int sig, const char* version, long timestamp,
                    const char* writer, long process_started) {
    struct stat st {};
    if (stat(path.c_str(), &st) == 0 && static_cast<long>(st.st_mtime) < process_started) {
        unlink(path.c_str());
    }
    // O_EXCL: a record that survived the check above is the dying process's own.
    const int fd = open(path.c_str(), O_CREAT | O_EXCL | O_WRONLY, 0644);
    if (fd < 0) {
        return false;
    }
    char buf[256];
    const int n = std::snprintf(buf, sizeof(buf),
                                "signal:%d\nname:%s\nversion:%s\ntimestamp:%ld\nrecorded_by:%s\n",
                                sig, signal_name(sig), version, timestamp, writer);
    const bool ok = n > 0 && n < static_cast<int>(sizeof(buf)) && write(fd, buf, n) == n;
    close(fd);
    if (!ok) {
        unlink(path.c_str());
    }
    return ok;
}

} // namespace helix::crash_record
