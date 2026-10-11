// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "usb_automount.h"

#if defined(__linux__) && !defined(__ANDROID__)

#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace helix::test {

/// Mount-call record format: "dev|mnt|fs|opts".
inline std::string format_mount_call(const std::string& dev, const std::string& mnt,
                                     const std::string& fs, const std::string& opts) {
    return dev + "|" + mnt + "|" + fs + "|" + opts;
}

/// Unmount-call record format: "mnt|plain" or "mnt|lazy".
inline std::string format_unmount_call(const std::string& mnt, bool lazy) {
    return mnt + "|" + (lazy ? "lazy" : "plain");
}

/// Records every syscall the automounter makes so assertions run against the
/// decision layer, not against a real mount table. The record is guarded by a
/// mutex: the automounter can run on a backend's monitor thread while the test
/// reads progress. The configuration fields are the test's to set before the
/// automounter starts.
class FakeMountOps : public helix::usb::MountOps {
  public:
    bool root = true;
    std::vector<std::string> candidates; // what a sysfs scan would return
    std::set<std::string> mounted;       // devices listed in the mount table
    std::set<std::string> present;       // devices sysfs still shows
    std::set<std::string> busy;          // mount points whose plain umount is EBUSY
    bool dir_result = true;

    // Which (fs, options) pairs this kernel accepts. Default: any vfat.
    std::function<bool(const std::string&, const std::string&)> accepts =
        [](const std::string& fs, const std::string&) { return fs == "vfat"; };

    size_t mount_count() const {
        std::lock_guard<std::mutex> lock(record_mutex_);
        return mount_calls_.size();
    }

    /// Snapshot of every mount attempt, oldest first.
    std::vector<std::string> mount_record() const {
        std::lock_guard<std::mutex> lock(record_mutex_);
        return mount_calls_;
    }

    /// Snapshot of every unmount attempt, including ones that fail with EBUSY,
    /// so assertions can see the plain-to-lazy fallback sequence.
    std::vector<std::string> unmount_record() const {
        std::lock_guard<std::mutex> lock(record_mutex_);
        return unmount_calls_;
    }

    bool is_root() override {
        return root;
    }
    std::vector<std::string> mounted_devices() override {
        return {mounted.begin(), mounted.end()};
    }
    std::vector<std::string> removable_block_devices() override {
        return candidates;
    }
    bool device_present(const std::string& device) override {
        return present.count(device) > 0;
    }
    bool ensure_mount_point_dir(const std::string&) override {
        return dir_result;
    }

    bool mount(const std::string& dev, const std::string& mnt, const std::string& fs,
               const std::string& opts) override {
        {
            std::lock_guard<std::mutex> lock(record_mutex_);
            mount_calls_.push_back(format_mount_call(dev, mnt, fs, opts));
        }
        return accepts(fs, opts);
    }

    bool unmount(const std::string& mnt, bool lazy) override {
        {
            std::lock_guard<std::mutex> lock(record_mutex_);
            unmount_calls_.push_back(format_unmount_call(mnt, lazy));
        }
        return !(!lazy && busy.count(mnt) > 0); // EBUSY on plain umount
    }

  private:
    mutable std::mutex record_mutex_;
    std::vector<std::string> mount_calls_;   // "dev|mnt|fs|opts"
    std::vector<std::string> unmount_calls_; // "mnt|plain" or "mnt|lazy"
};

/// MountOps handle that shares a fake instead of owning it. The automounter
/// destroys its ops when it is torn down, and a backend test wants the record
/// from AFTER that teardown: stop() joins the monitor thread, the thread
/// unmounts during the join, and stop() then releases the automounter. Shared
/// ownership keeps the fake readable past that release.
class SharedMountOps : public helix::usb::MountOps {
  public:
    explicit SharedMountOps(std::shared_ptr<FakeMountOps> fake) : fake_(std::move(fake)) {}

    bool is_root() override {
        return fake_->is_root();
    }
    std::vector<std::string> mounted_devices() override {
        return fake_->mounted_devices();
    }
    std::vector<std::string> removable_block_devices() override {
        return fake_->removable_block_devices();
    }
    bool device_present(const std::string& device) override {
        return fake_->device_present(device);
    }
    bool ensure_mount_point_dir(const std::string& mnt) override {
        return fake_->ensure_mount_point_dir(mnt);
    }
    bool mount(const std::string& dev, const std::string& mnt, const std::string& fs,
               const std::string& opts) override {
        return fake_->mount(dev, mnt, fs, opts);
    }
    bool unmount(const std::string& mnt, bool lazy) override {
        return fake_->unmount(mnt, lazy);
    }

  private:
    std::shared_ptr<FakeMountOps> fake_;
};

} // namespace helix::test

#endif // __linux__ && !__ANDROID__
