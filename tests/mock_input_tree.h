// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "input_device_scanner.h"

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <map>
#include <string>
#include <system_error>

namespace helix::test {

// Build a sysfs capability string the way the kernel prints bitmaps at the
// running machine's word width: space-separated hex words, highest word
// first, each via %llx with no zero padding, leading zero words stripped.
// Mock caps built with this parse exactly like real sysfs on whatever machine
// runs the tests (64-bit dev/CI and 32-bit ARM targets alike).
inline std::string caps_string(std::initializer_list<int> bits) {
    const int word_bits = helix::input::sysfs_bitmap_word_bits();
    std::map<int, unsigned long long> words; // keyed by word-from-right
    int highest_nonzero = -1;
    for (int bit : bits) {
        const int word = bit / word_bits;
        words[word] |= 1ULL << (bit % word_bits);
        highest_nonzero = std::max(highest_nonzero, word);
    }

    std::string out;
    for (int word = (highest_nonzero < 0 ? 0 : highest_nonzero); word >= 0; --word) {
        if (!out.empty())
            out += ' ';
        char buf[24];
        std::snprintf(buf, sizeof(buf), "%llx", words[word]);
        out += buf;
    }
    return out;
}

// Key caps of a typical USB mouse: BTN_LEFT..BTN_EXTRA (keycodes 272-276).
// On a 64-bit kernel this prints "1f0000 0 0 0 0", on 32-bit
// "1f0000 0 0 0 0 0 0 0 0", both real field strings.
inline std::string mouse_key_caps() {
    return caps_string({272, 273, 274, 275, 276});
}

/// A throwaway /dev/input and /sys/class/input pair for the input scanner to read.
struct MockInputTree {
    std::string base;
    std::string dev_dir;
    std::string sysfs_dir;

    explicit MockInputTree(const std::string& label) {
        base = "/tmp/helix_test_input_" + label + "_" +
               std::to_string(static_cast<unsigned long>(time(nullptr)));
        dev_dir = base + "/dev/input";
        sysfs_dir = base + "/sys/class/input";
        std::filesystem::create_directories(dev_dir);
        std::filesystem::create_directories(sysfs_dir);
    }

    ~MockInputTree() {
        std::error_code ec;
        std::filesystem::remove_all(base, ec);
    }

    // bustype: "0003"=USB, "0005"=Bluetooth, "0019"=host/platform, ""=omit
    void add_device(int event_num, const std::string& name,
                    const std::map<std::string, std::string>& caps,
                    const std::string& bustype = "0003") {
        std::string dev_path = dev_dir + "/event" + std::to_string(event_num);
        std::ofstream(dev_path).put('x');

        std::string sysfs_path = sysfs_dir + "/event" + std::to_string(event_num);
        std::filesystem::create_directories(sysfs_path + "/device/capabilities");
        std::filesystem::create_directories(sysfs_path + "/device/id");

        std::ofstream(sysfs_path + "/device/name") << name;

        if (!bustype.empty()) {
            std::ofstream(sysfs_path + "/device/id/bustype") << bustype;
        }

        for (const auto& [cap_name, hex_value] : caps) {
            std::ofstream(sysfs_path + "/device/capabilities/" + cap_name) << hex_value;
        }

        // Write vendor/product ID files if the bustype is USB or Bluetooth
        if (bustype == "0003" || bustype == "0005") {
            std::ofstream(sysfs_path + "/device/id/vendor") << "1a2c";
            std::ofstream(sysfs_path + "/device/id/product")
                << std::string("000") + std::to_string(event_num);
        }
    }

    void add_device_with_ids(int event_num, const std::string& name,
                             const std::map<std::string, std::string>& caps,
                             const std::string& bustype, const std::string& vendor,
                             const std::string& product) {
        add_device(event_num, name, caps, bustype);
        std::string sysfs_path = sysfs_dir + "/event" + std::to_string(event_num);
        // Overwrite the default IDs written by add_device
        std::ofstream(sysfs_path + "/device/id/vendor") << vendor;
        std::ofstream(sysfs_path + "/device/id/product") << product;
    }
};

} // namespace helix::test
