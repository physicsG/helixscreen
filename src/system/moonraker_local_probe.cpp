// Copyright (C) 2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "system/moonraker_local_probe.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace helix::diag {

namespace {

// TCP_LISTEN in the kernel's st column.
constexpr const char* STATE_LISTEN = "0A";

bool parse_hex(const std::string& s, uint32_t& out) {
    if (s.empty() || s.size() > 8)
        return false;
    uint32_t v = 0;
    for (char c : s) {
        int d;
        if (c >= '0' && c <= '9')
            d = c - '0';
        else if (c >= 'a' && c <= 'f')
            d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F')
            d = c - 'A' + 10;
        else
            return false;
        v = (v << 4) | static_cast<uint32_t>(d);
    }
    out = v;
    return true;
}

// One %08X group back into its four bytes, in the order the kernel held them.
// Going through the host's own representation of the parsed integer is what
// makes this endian-agnostic — see the header note.
void group_to_bytes(uint32_t raw, unsigned char out[4]) {
    std::memcpy(out, &raw, 4);
}

std::string decode_ipv4(const std::string& hex) {
    uint32_t raw = 0;
    if (hex.size() != 8 || !parse_hex(hex, raw))
        return {};
    unsigned char b[4];
    group_to_bytes(raw, b);
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
    return buf;
}

std::string decode_ipv6(const std::string& hex) {
    if (hex.size() != 32)
        return {};
    unsigned char b[16];
    for (int g = 0; g < 4; ++g) {
        uint32_t raw = 0;
        if (!parse_hex(hex.substr(static_cast<size_t>(g) * 8, 8), raw))
            return {};
        group_to_bytes(raw, b + g * 4);
    }

    const bool all_zero = std::all_of(b, b + 16, [](unsigned char c) { return c == 0; });
    if (all_zero)
        return "::";
    const bool loopback =
        std::all_of(b, b + 15, [](unsigned char c) { return c == 0; }) && b[15] == 1;
    if (loopback)
        return "::1";

    // No :: compression beyond the two cases above — this is a diagnostic
    // string, and an uncompressed form is unambiguous rather than pretty.
    std::string out;
    char buf[8];
    for (int i = 0; i < 16; i += 2) {
        std::snprintf(buf, sizeof(buf), "%x", (b[i] << 8) | b[i + 1]);
        if (i)
            out += ':';
        out += buf;
    }
    return out;
}

} // namespace

std::vector<std::string> parse_listeners_for_port(const std::string& proc_net_tcp, uint16_t port,
                                                  bool ipv6) {
    std::vector<std::string> out;
    std::istringstream in(proc_net_tcp);
    std::string line;
    bool first = true;

    while (std::getline(in, line)) {
        if (first) { // "sl  local_address rem_address st ..." header
            first = false;
            continue;
        }

        std::istringstream ls(line);
        std::string sl, local, remote, state;
        if (!(ls >> sl >> local >> remote >> state))
            continue;
        if (state != STATE_LISTEN)
            continue;

        const size_t colon = local.rfind(':');
        if (colon == std::string::npos)
            continue;
        uint32_t got_port = 0;
        if (!parse_hex(local.substr(colon + 1), got_port))
            continue;
        if (got_port != port)
            continue;

        const std::string addr_hex = local.substr(0, colon);
        const std::string addr = ipv6 ? decode_ipv6(addr_hex) : decode_ipv4(addr_hex);
        if (addr.empty())
            continue;

        // Bracket a v6 address so "::1:7125" cannot be misread.
        out.push_back(ipv6 ? "[" + addr + "]:" + std::to_string(port)
                           : addr + ":" + std::to_string(port));
    }

    return out;
}

bool split_host_port(const std::string& base_url, std::string& host, uint16_t& port,
                     uint16_t fallback) {
    std::string s = base_url;

    uint16_t scheme_port = fallback;
    const size_t scheme_end = s.find("://");
    if (scheme_end != std::string::npos) {
        const std::string scheme = s.substr(0, scheme_end);
        if (scheme == "https" || scheme == "wss")
            scheme_port = 443;
        else if (scheme == "http" || scheme == "ws")
            scheme_port = 80;
        s.erase(0, scheme_end + 3);
    }

    // Strip path/query before looking for the port, or "/a:b" would parse as one.
    const size_t path = s.find_first_of("/?#");
    if (path != std::string::npos)
        s.erase(path);
    if (s.empty())
        return false;

    if (s.front() == '[') { // [::1]:7125
        const size_t close = s.find(']');
        if (close == std::string::npos)
            return false;
        host = s.substr(1, close - 1);
        const std::string rest = s.substr(close + 1);
        port = scheme_port;
        if (rest.size() > 1 && rest.front() == ':') {
            uint32_t p = 0;
            if (std::sscanf(rest.c_str() + 1, "%u", &p) == 1 && p > 0 && p <= 65535)
                port = static_cast<uint16_t>(p);
        }
        return !host.empty();
    }

    const size_t colon = s.rfind(':');
    // An unbracketed address with several colons is a bare IPv6 literal, not
    // host:port — do not amputate its last group.
    const bool bare_v6 = colon != std::string::npos && s.find(':') != colon;
    if (colon == std::string::npos || bare_v6) {
        host = s;
        port = scheme_port;
        return !host.empty();
    }

    host = s.substr(0, colon);
    port = scheme_port;
    uint32_t p = 0;
    if (std::sscanf(s.c_str() + colon + 1, "%u", &p) == 1 && p > 0 && p <= 65535)
        port = static_cast<uint16_t>(p);
    return !host.empty();
}

std::string decode_proc_cmdline(const std::string& raw) {
    std::string s = raw;
    while (!s.empty() && s.back() == '\0')
        s.pop_back();
    std::replace(s.begin(), s.end(), '\0', ' ');
    return s;
}

bool cmdline_matches_any(const std::string& cmdline, const std::vector<std::string>& needles) {
    return std::any_of(needles.begin(), needles.end(), [&](const std::string& n) {
        return !n.empty() && cmdline.find(n) != std::string::npos;
    });
}

namespace {

// argv split on spaces. decode_proc_cmdline() already collapsed the NULs, and a
// path with a space in it would have survived as one argv entry there — so this
// can mis-split such a path. Accepted: the alternative is threading the raw NUL
// form through, and a printer_data directory with a space in its name has never
// been seen in a bundle.
std::vector<std::string> split_args(const std::string& cmdline) {
    std::vector<std::string> args;
    std::istringstream in(cmdline);
    std::string tok;
    while (in >> tok)
        args.push_back(tok);
    return args;
}

// Value of `-x VALUE`, `--long VALUE`, or `--long=VALUE`.
std::string flag_value(const std::vector<std::string>& args, const std::string& shortf,
                       const std::string& longf) {
    for (size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        if ((a == shortf || a == longf) && i + 1 < args.size())
            return args[i + 1];
        const std::string eq = longf + "=";
        if (a.compare(0, eq.size(), eq) == 0 && a.size() > eq.size())
            return a.substr(eq.size());
    }
    return {};
}

std::string parent_of(const std::string& path) {
    const size_t slash = path.rfind('/');
    return slash == std::string::npos ? std::string{} : path.substr(0, slash);
}

} // namespace

LogPathHints parse_log_hints(const std::string& cmdline) {
    const auto args = split_args(cmdline);
    LogPathHints h;
    h.log_file = flag_value(args, "-l", "--logfile");
    h.data_path = flag_value(args, "-d", "--datapath");
    h.config_file = flag_value(args, "-c", "--configfile");
    return h;
}

std::vector<std::string> candidate_log_paths(const std::vector<ProcMatch>& procs,
                                             const std::string& log_name) {
    std::vector<std::string> out;
    auto add = [&out](const std::string& p) {
        if (p.empty() || p.front() != '/')
            return; // absolute only — a relative path would resolve against OUR cwd
        if (std::find(out.begin(), out.end(), p) == out.end())
            out.push_back(p);
    };

    for (const auto& proc : procs) {
        const auto h = parse_log_hints(proc.cmdline);

        // -l names a log file directly. Take it when it IS the file we want, and
        // otherwise use its directory: klippy and moonraker keep their logs side
        // by side, so one daemon's -l locates the other's log too.
        if (!h.log_file.empty()) {
            const size_t slash = h.log_file.rfind('/');
            const std::string base =
                slash == std::string::npos ? h.log_file : h.log_file.substr(slash + 1);
            if (base == log_name)
                add(h.log_file);
            else
                add(parent_of(h.log_file) + "/" + log_name);
        }

        if (!h.data_path.empty())
            add(h.data_path + "/logs/" + log_name);

        // <data>/config/printer.cfg -> <data>/logs/<name>
        if (!h.config_file.empty()) {
            const std::string cfg_dir = parent_of(h.config_file);
            add(parent_of(cfg_dir) + "/logs/" + log_name);
            add(cfg_dir + "/" + log_name);
        }
    }

    return out;
}

std::vector<std::string> listeners_on_port(uint16_t port) {
    std::vector<std::string> out;
    const std::pair<const char*, bool> sources[] = {{"/proc/net/tcp", false},
                                                    {"/proc/net/tcp6", true}};
    for (const auto& [path, is_v6] : sources) {
        std::ifstream f(path, std::ios::binary);
        if (!f.is_open())
            continue;
        std::string body((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        auto found = parse_listeners_for_port(body, port, is_v6);
        out.insert(out.end(), found.begin(), found.end());
    }
    return out;
}

std::vector<ProcMatch> find_moonraker_processes() {
    // "klippy" rather than "klipper": the process is klippy.py, and matching
    // "klipper" alone would also hit every path containing the klipper dir.
    static const std::vector<std::string> NEEDLES = {"moonraker", "klippy"};

    std::vector<ProcMatch> out;
    std::error_code ec;
    if (!fs::is_directory("/proc", ec))
        return out;

    for (const auto& entry : fs::directory_iterator("/proc", ec)) {
        if (ec)
            break;
        const std::string name = entry.path().filename().string();
        if (name.empty() ||
            !std::all_of(name.begin(), name.end(), [](unsigned char c) { return std::isdigit(c); }))
            continue;

        std::ifstream cmd(entry.path() / "cmdline", std::ios::binary);
        if (!cmd.is_open())
            continue;
        std::string raw((std::istreambuf_iterator<char>(cmd)), std::istreambuf_iterator<char>());
        const std::string cmdline = decode_proc_cmdline(raw);
        if (cmdline.empty() || !cmdline_matches_any(cmdline, NEEDLES))
            continue;

        // Skip ourselves: helix-screen's own argv can name a moonraker URL.
        if (cmdline.find("helix-screen") != std::string::npos)
            continue;

        ProcMatch m;
        m.pid = std::strtol(name.c_str(), nullptr, 10);
        m.cmdline = cmdline;
        out.push_back(std::move(m));
    }

    return out;
}

} // namespace helix::diag
